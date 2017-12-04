/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <skalibs/types.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/bitarray.h>
#include <skalibs/cdb.h>
#include <skalibs/stralloc.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/skamisc.h>
#include <skalibs/unix-transactional.h>
#include <execline/execline.h>
#include <s6/config.h>
#include <s6/s6-supervise.h>
#include <s6/s6-fdholder.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-update [ -n ] [ -v verbosity ] [ -t timeout ] [ -l live ] [ -f conversion_file ] [ -b ] newdb"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "build string") ;

static char const *live = S6RC_LIVE_BASE ;
static size_t livelen = sizeof(S6RC_LIVE_BASE) - 1 ;
static unsigned int verbosity = 1 ;



 /* Conversions and transitions */


 /*
  oldstate flags:
     1 -> is up
     2 -> wanted down
     4 -> restart
     8 -> converts to atomic or singleton
    16 -> has a new name
    32 -> wanted down after closure
    64 -> appears in convfile

  newstate flags:
     1 -> is up (converted from old up)
     2 -> wanted up
     4 -> is a bijective conversion target
     8 -> is a conversion target
    16 -> changed names
    32 -> is up after closure (i.e. includes new deps)
   128 -> depends on a new service, has to be restarted
 */

static inline void parse_line (stralloc *sa, char const *s, size_t slen, unsigned int *newnames, unsigned char *oldstate, cdb_t *oldc, s6rc_db_t const *olddb)
{
  size_t base = sa->len ;
  unsigned int oldn = olddb->nshort + olddb->nlong ;
  int n ;
  if (!stralloc_readyplus(sa, slen)) dienomem() ;
  sa->len += slen ;
  n = el_parse_from_string(sa, s) ;
  switch (n)
  {
    case -1 : dienomem() ;
    case -2 : strerr_dief2x(100, "syntax error in conversion file: ", s) ; 
    case -3 : strerr_dief2x(100, "mismatched braces in conversion file: ", s) ; 
    default : break ;
  }
  sa->len = base ;
  if (n--)
  {
    char pack[4] ;
    uint32_t x ;
    unsigned int cur ;
    int r = cdb_find(oldc, sa->s + base + slen, strlen(sa->s + base + slen)) ;
    if (r < 0) strerr_diefu3sys(111, "read ", live, "/compiled/resolve.cdb") ;
    if (!r) strerr_dief5x(3, "unknown identifier in ", live, "/compiled/resolve.cdb", ": ", sa->s + base + slen) ;
    if (cdb_datalen(oldc) != 4) strerr_dief5x(5, "identifier ", sa->s + base + slen, " does not represent an atomic service in ", live, "/compiled") ;
    if (cdb_read(oldc, pack, 4, cdb_datapos(oldc)) < 0)
      strerr_diefu3sys(111, "read ", live, "/compiled/resolve.cdb") ;
    uint32_unpack_big(pack, &x) ;
    if (x >= oldn) strerr_dief3x(4, "invalid database in ", live, "/compiled") ;
    if (oldstate[x] & 64)
      strerr_dief3x(6, "service ", olddb->string + olddb->services[x].name, " appears more than once in conversion file") ;
    oldstate[x] |= 64 ;
    cur = base + slen + strlen(sa->s + base + slen) + 1 ;
    if (n >= 2 && !strcmp(sa->s + cur, "->"))
    {
      size_t newnamelen = strlen(sa->s + cur + 3) ;
      memcpy(sa->s + sa->len, sa->s + cur + 3, newnamelen + 1) ;
      newnames[x] = sa->len ; oldstate[x] |= 16 ;
      sa->len += newnamelen + 1 ;
      cur += newnamelen + 4 ;
      n -= 2 ;
    }
    while (n--)
    {
      if (!strcmp(sa->s + cur, "restart")) oldstate[x] |= 4 ;
      else
        strerr_dief2x(100, "unknown keyword in conversion file: ", sa->s + cur) ;
      cur += strlen(sa->s + cur) + 1 ;
    }
  }
}

static inline void parse_conversion_file (char const *convfile, stralloc *sa, unsigned int *newnames, unsigned char *oldstate, cdb_t *oldc, s6rc_db_t const *olddb)
{
  int fd = open_readb(convfile) ;
  char buf[4096] ;
  buffer b = BUFFER_INIT(&buffer_read, fd, buf, 4096) ;
  size_t base = satmp.len ;
  if (fd < 0) strerr_diefu2sys(111, "open ", convfile) ;

  for (;;)
  {
    int r = skagetln(&b, &satmp, '\n') ;
    if (!r) break ;
    if (r < 0)
    {
      if (errno != EPIPE) strerr_diefu1sys(111, "read from stdin") ;
      if (!stralloc_0(&satmp)) dienomem() ;
    }
    else satmp.s[satmp.len - 1] = 0 ;
    parse_line(sa, satmp.s + base, satmp.len - base, newnames, oldstate, oldc, olddb) ;
    satmp.len = base ;
  }

  satmp.len = base ;
  close(fd) ;
}

static inline void stuff_with_oldc (unsigned char *oldstate, int fdoldc, s6rc_db_t const *olddb, char const *convfile, unsigned int *oldindex, stralloc *namedata)
{
  cdb_t oldc = CDB_ZERO ;
  int oldfdres = open_readatb(fdoldc, "resolve.cdb") ;
  if (oldfdres < 0) strerr_diefu3sys(111, "open ", live, "/compiled/resolve.cdb") ;
  if (!cdb_init_map(&oldc, oldfdres, 1))
    strerr_diefu3sys(111, "cdb_init ", live, "/compiled/resolve.cdb") ;

  parse_conversion_file(convfile, namedata, oldindex, oldstate, &oldc, olddb) ;

  cdb_free(&oldc) ;
  close(oldfdres) ;
}

static inline void fill_convtable_and_flags (unsigned char *conversion_table, unsigned char *oldstate, unsigned char *newstate, char const *namedata, unsigned int const *oldindex, unsigned int *invimage, cdb_t *newc, char const *newfn, s6rc_db_t const *olddb, s6rc_db_t const *newdb)
{
  unsigned int newn = newdb->nshort + newdb->nlong ;
  unsigned int i = olddb->nshort + olddb->nlong ;

  while (i--)
  {
    char const *newname = oldstate[i] & 16 ? namedata + oldindex[i] : olddb->string + olddb->services[i].name ;
    uint32_t len ;
    int r = cdb_find(newc, newname, strlen(newname)) ;
    if (r < 0) strerr_diefu3sys(111, "read ", newfn, "/resolve.cdb") ;
    if (!r)
    {
      if (oldstate[i] & 16)
        strerr_dief4x(4, "bad conversion file: new service ", newname, " is undefined in database ", newfn) ;
      oldstate[i] |= 34 ; /* disappeared */
      continue ;
    }
    if (cdb_datalen(newc) & 3)
      strerr_dief3x(4, "invalid resolve database in ", newfn, "/resolve.cdb") ;
    len = cdb_datalen(newc) >> 2 ;
    if (len > newn)
      strerr_dief3x(4, "invalid resolve database in ", newfn, "/resolve.cdb") ;
    {
      char pack[cdb_datalen(newc) + 1] ;
      char const *p = pack ;
      if (cdb_read(newc, pack, cdb_datalen(newc), cdb_datapos(newc)) < 0)
        strerr_diefu3sys(111, "read ", newfn, "/resolve.cdb") ;
      if (len == 1) oldstate[i] |= 8 ;
      while (len--)
      {
        uint32_t x ;
        uint32_unpack_big(p, &x) ; p += 4 ;
        if (x >= newn)
          strerr_dief3x(4, "invalid resolve database in ", newfn, "/resolve.cdb") ;
        if (newstate[x] & 8)
          strerr_diefu4x(6, "convert database: new service ", newdb->string + newdb->services[x].name, " is a target for more than one conversion, including old service ", olddb->string + olddb->services[i].name) ;
        newstate[x] |= 8 ;
        invimage[x] = i ;
        if (oldstate[i] & 16) newstate[x] |= 16 ;
        bitarray_set(conversion_table + i * bitarray_div8(newn), x) ;
        if (oldstate[i] & 8)
        {
          newstate[x] |= 4 ;
          if ((i < olddb->nlong) != (x < newdb->nlong))
            oldstate[i] |= 4 ;
        }
      }
    }
  }
}

static inline void stuff_with_newc (int fdnewc, char const *newfn, unsigned char *conversion_table, unsigned char *oldstate, unsigned char *newstate, char const *namedata, unsigned int const *oldindex, unsigned int *invimage, s6rc_db_t const *olddb, s6rc_db_t const *newdb)
{
  cdb_t newc = CDB_ZERO ;
  int newfdres = open_readatb(fdnewc, "resolve.cdb") ;
  if (newfdres < 0) strerr_diefu3sys(111, "open ", newfn, "/compiled/resolve.cdb") ;
  if (!cdb_init_map(&newc, newfdres, 1))
    strerr_diefu3sys(111, "cdb_init ", newfn, "/compiled/resolve.cdb") ;

  fill_convtable_and_flags(conversion_table, oldstate, newstate, namedata, oldindex, invimage, &newc, newfn, olddb, newdb) ;

  cdb_free(&newc) ;
  close(newfdres) ;
}

static void compute_transitions (char const *convfile, unsigned char *oldstate, int fdoldc, s6rc_db_t const *olddb, unsigned char *newstate, unsigned int *invimage, int fdnewc, char const *newfn, s6rc_db_t const *newdb, stralloc *sa)
{
  unsigned int oldn = olddb->nshort + olddb->nlong ;
  unsigned int newn = newdb->nshort + newdb->nlong ;
  unsigned int newm = bitarray_div8(newn) ;
  unsigned int oldindex[oldn] ;
  unsigned char conversion_table[oldn * newm] ;
  memset(conversion_table, 0, oldn * newm) ;
  stuff_with_oldc(oldstate, fdoldc, olddb, convfile, oldindex, sa) ;
  stuff_with_newc(fdnewc, newfn, conversion_table, oldstate, newstate, sa->s, oldindex, invimage, olddb, newdb) ;
  sa->len = 0 ;

  for (;;)
  {
    int done = 1 ;
    unsigned int i = newn ;
    while (i--) newstate[i] &= 28 ;

   /*
     If an old service needs to restart, mark it wanted down, as well
     as everything that depends on it.
   */

    i = oldn ;
    while (i--)
    {
      if (oldstate[i] & 1 && (oldstate[i] & 4 || !(oldstate[i] & 8)))
        oldstate[i] |= 34 ;
      else oldstate[i] &= 221 ;
    }
    s6rc_graph_closure(olddb, oldstate, 5, 0) ;


   /*
      Convert the old state to the new state: if an old service is up,
      the new service will be either up or wanted up.
      This part runs in O(oldn*newn). There are no syscalls in the loop,
      so it should still be negligible unless you have 10k services.
   */

    i = oldn ;
    while (i--) if (oldstate[i] & 1)
    {
      unsigned int j = newn ;
      while (j--) if (bitarray_peek(conversion_table + i * newm, j))
        newstate[j] |= (oldstate[i] & 32) ? 2 : 33 ;
    }


   /*
     Check for new dependencies. If a new service is still up but depends on a
     new service that is not, it has to be restarted. Mark the old service for
     restart and loop until there are no new dependencies.
   */

    s6rc_graph_closure(newdb, newstate, 5, 1) ;
    i = newn ;
    while (i--) if ((newstate[i] & 33) == 32)
    {
      done = 0 ;
      newstate[i] |= 128U ;
    }
    if (done) break ;
    s6rc_graph_closure(newdb, newstate, 7, 0) ;
    i = newn ;
    while (i--) if ((newstate[i] & 129U) == 129U) oldstate[invimage[i]] |= 4 ;
  }
}



 /* Update the live directory while keeping active servicedirs */


static inline void rollback_servicedirs (char const *newlive, unsigned char const *newstate, unsigned int const *invimage, s6rc_db_t const *olddb, s6rc_db_t const *newdb, unsigned int n)
{
  size_t newllen = strlen(newlive) ;
  unsigned int i = n ;
  while (i--)
  {
    size_t newnamelen = strlen(newdb->string + newdb->services[i].name) ;
    char newfn[newllen + 14 + newnamelen] ;
    memcpy(newfn, newlive, newllen) ;
    memcpy(newfn + newllen, "/servicedirs/", 13) ;
    memcpy(newfn + newllen + 13, newdb->string + newdb->services[i].name, newnamelen + 1) ;
    if (newstate[i] & 1)
    {
      char const *oldname = newstate[i] & 8 ? olddb->string + olddb->services[invimage[i]].name : newdb->string + newdb->services[i].name ;
      size_t oldnamelen = strlen(oldname) ;
      char oldfn[livelen + 23 + oldnamelen] ;
      memcpy(oldfn, live, livelen) ;
      memcpy(oldfn + livelen, "/compiled/servicedirs/", 22) ;
      memcpy(oldfn + livelen + 22, oldname, oldnamelen + 1) ;
      if (!s6rc_servicedir_copy_online(oldfn, newfn))
        strerr_diefu4sys(111, "rollback ", oldfn, " into ", newfn) ;
      memcpy(oldfn + livelen, "/servicedirs/", 13) ;
      memcpy(oldfn + livelen + 13, oldname, oldnamelen + 1) ;
      if (rename(newfn, oldfn) < 0)
        strerr_diefu4sys(111, "rollback: can't rename ", newfn, " to ", oldfn) ;
    }
  }
}

static inline void make_new_livedir (unsigned char const *oldstate, s6rc_db_t const *olddb, unsigned char const *newstate, s6rc_db_t const *newdb, char const *newcompiled, unsigned int *invimage, char const *prefix, stralloc *sa)
{
  size_t dirlen, pos, newlen, sdlen ;
  size_t newclen = strlen(newcompiled) ;
  unsigned int i = newdb->nlong + newdb->nshort ;
  if (sareadlink(sa, live) < 0 || !stralloc_0(sa)) strerr_diefu2sys(111, "readlink ", live) ;
  pos = sa->len ;
  {
    ssize_t r ;
    char sdtarget[PATH_MAX] ;
    char sdlink[livelen + 9] ;
    unsigned char tmpstate[newdb->nlong + newdb->nshort] ;
    memcpy(sdlink, live, livelen) ;
    memcpy(sdlink + livelen, "/scandir", 9) ;
    r = readlink(sdlink, sdtarget, PATH_MAX) ;
    if (r < 0) strerr_diefu2sys(111, "readlink ", sdlink) ;
    if (r >= PATH_MAX - 1) strerr_dief3x(100, "target for ", sdlink, " is too long") ;
    sdtarget[r] = 0 ;
    while (i--) tmpstate[i] = newstate[i] & 1 ;
    if (!s6rc_livedir_create(sa, live, PROG, sdtarget, prefix, newcompiled, tmpstate, newdb->nlong + newdb->nshort, &dirlen))
      strerr_diefu1sys(111, "create new livedir") ;
  }
  newlen = sa->len ;
  i = 0 ;
  if (!stralloc_catb(sa, "/servicedirs/", 13)) goto rollback ;
  sdlen = sa->len ;

  for (; i < newdb->nlong ; i++)
  {
    size_t newnamelen = strlen(newdb->string + newdb->services[i].name) ;
    char newfn[newclen + 14 + newnamelen] ;
    memcpy(newfn, newcompiled, newclen) ;
    memcpy(newfn + newclen, "/servicedirs/", 13) ;
    memcpy(newfn + newclen + 13, newdb->string + newdb->services[i].name, newnamelen + 1) ;
    sa->len = sdlen ;
    if (!stralloc_cats(sa, newdb->string + newdb->services[i].name) || !stralloc_0(sa)) goto rollback ;
    if (newstate[i] & 1)
    {
      char const *oldname = newstate[i] & 8 ? olddb->string + olddb->services[invimage[i]].name : newdb->string + newdb->services[i].name ;
      size_t oldnamelen = strlen(oldname) ;
      char oldfn[livelen + 14 + oldnamelen] ;
      memcpy(oldfn, live, livelen) ;
      memcpy(oldfn + livelen, "/servicedirs/", 13) ;
      memcpy(oldfn + livelen + 13, oldname, oldnamelen + 1) ;
      if (rename(oldfn, sa->s + pos) < 0) goto rollback ;
      if (!s6rc_servicedir_copy_online(newfn, sa->s + pos)) { i++ ; goto rollback ; }
    }
    else if (!s6rc_servicedir_copy_offline(newfn, sa->s + pos)) goto rollback ;
  }
  sa->len = newlen ;
  sa->s[sa->len] = 0 ;

   /* The point of no return is here */
  if (!atomic_symlink(sa->s + dirlen, live, "s6-rc-update_atomic_symlink")) goto rollback ;
  if (verbosity >= 2) strerr_warni1x("successfully switched to new database") ;

 /* scandir cleanup, then old livedir cleanup */
  i = olddb->nlong ;
  while (i--)
    s6rc_servicedir_unsupervise(sa->s, prefix, olddb->string + olddb->services[i].name, (oldstate[i] & 33) == 1) ;
  rm_rf_in_tmp(sa, 0) ;
  sa->len = 0 ;
  return ;

 rollback:
  {
    int e = errno ;
    sa->len = newlen ;
    sa->s[sa->len++] = 0 ;
    rollback_servicedirs(sa->s + pos, newstate, invimage, olddb, newdb, i) ;
    rm_rf_in_tmp(sa, 0) ;
    errno = e ;
  }
  strerr_diefu2sys(111, "make new live directory in ", sa->s) ;
}


 /* Updating the pipes contained in the fdholder */

static inline int delete_unused_pipes (s6_fdholder_t *a, s6rc_db_t const *olddb, unsigned char const *oldstate, tain_t const *deadline)
{
  unsigned int i = olddb->nlong ;
  while (i--)
    if (!(oldstate[i] & 8) && olddb->services[i].x.longrun.nproducers)
    {
      size_t len = strlen(olddb->string + olddb->services[i].name) ;
      char pipename[len + 13] ;
      memcpy(pipename, "pipe:s6rc-w-", 12) ;
      memcpy(pipename + 12, olddb->string + olddb->services[i].name, len + 1) ;
      if (!s6_fdholder_delete_g(a, pipename, deadline)) return 0 ;
      pipename[10] = 'r' ;
      if (!s6_fdholder_delete_g(a, pipename, deadline)) return 0 ;
    }
  return 1 ;
}

static inline int rename_pipes (s6_fdholder_t *a, s6rc_db_t const *olddb, s6rc_db_t const *newdb, unsigned char const *newstate, unsigned int const *invimage, tain_t const *deadline)
{
  tain_t nano1 = { .sec = TAI_ZERO, .nano = 1 } ;
  tain_t limit ;
  tain_add_g(&limit, &tain_infinite_relative) ;
  unsigned int i = newdb->nlong ;
  while (i--)
  {
    if ((newstate[i] & 20) == 20 && newdb->services[i].x.longrun.nproducers)
    {
      int fd ;
      size_t oldlen = strlen(olddb->string + olddb->services[invimage[i]].name) ;
      size_t newlen = strlen(newdb->string + newdb->services[i].name) ;
      char oldpipename[oldlen + 13] ;
      char newpipename[newlen + 13] ;
      memcpy(oldpipename, "pipe:s6rc-r-", 12) ;
      memcpy(oldpipename + 12, olddb->string + olddb->services[invimage[i]].name, oldlen + 1) ;
      memcpy(newpipename, "pipe:s6rc-r-", 12) ;
      memcpy(newpipename + 12, newdb->string + newdb->services[i].name, newlen + 1) ;
      fd = s6_fdholder_retrieve_delete_g(a, oldpipename, deadline) ;
      if (fd < 0) return 0 ;
      if (!s6_fdholder_store_g(a, fd, newpipename, &limit, deadline))
      {
        close(fd) ;
        return 0 ;
      }
      close(fd) ;
      tain_add(&limit, &limit, &nano1) ;
      oldpipename[10] = 'w' ;
      newpipename[10] = 'w' ;
      fd = s6_fdholder_retrieve_delete_g(a, oldpipename, deadline) ;
      if (fd < 0) return 0 ;
      if (!s6_fdholder_store_g(a, fd, newpipename, &limit, deadline))
      {
        close(fd) ;
        return 0 ;
      }
      close(fd) ;
      tain_add(&limit, &limit, &nano1) ;
    }
  }
  return 1 ;
}

static inline int create_new_pipes (s6_fdholder_t *a, s6rc_db_t const *newdb, unsigned char const *newstate, tain_t const *deadline)
{
  tain_t nano1 = { .sec = TAI_ZERO, .nano = newdb->nlong % 1000000000U } ;
  tain_t limit ;
  unsigned int i = newdb->nlong ;
  tain_add_g(&limit, &tain_infinite_relative) ;
  tain_add(&limit, &limit, &nano1) ;
  nano1.nano = 1 ;
  while (i--)
  {
    if (!(newstate[i] & 4) && newdb->services[i].x.longrun.nproducers)
    {
      int p[2] ;
      size_t len = strlen(newdb->string + newdb->services[i].name) ;
      char pipename[len + 13] ;
      memcpy(pipename, "pipe:s6rc-r-", 12) ;
      memcpy(pipename + 12, newdb->string + newdb->services[i].name, len + 1) ;
      if (pipe(p) < 0) return 0 ;
      if (!s6_fdholder_store_g(a, p[0], pipename, &limit, deadline))
      {
        close(p[0]) ;
        close(p[1]) ;
        return 0 ;
      }
      close(p[0]) ;
      tain_add(&limit, &limit, &nano1) ;
      pipename[10] = 'w' ;
      if (!s6_fdholder_store_g(a, p[1], pipename, &limit, deadline))
      {
        close(p[1]) ;
        return 0 ;
      }
      close(p[1]) ;
      tain_add(&limit, &limit, &nano1) ;
    }
  }
  return 1 ;
}

static void fill_tfmt (char *tfmt, tain_t const *deadline)
{
  int t ;
  tain_t tto ;
  tain_sub(&tto, deadline, &STAMP) ;
  t = tain_to_millisecs(&tto) ;
  if (!t) t = 1 ;
  else if (t < 0) t = 0 ;
  tfmt[uint_fmt(tfmt, t)] = 0 ;
}

static inline void update_fdholder (s6rc_db_t const *olddb, unsigned char const *oldstate, s6rc_db_t const *newdb, unsigned char const *newstate, unsigned int const *invimage, char const *const *envp, tain_t const *deadline)
{
  s6_fdholder_t a = S6_FDHOLDER_ZERO ;
  char fnsocket[livelen + sizeof("/servicedirs/" S6RC_FDHOLDER "/s")] ;
  if (!(newstate[1] & 1)) return ;
  memcpy(fnsocket, live, livelen) ;
  memcpy(fnsocket + livelen, "/servicedirs/" S6RC_FDHOLDER "/s", sizeof("/servicedirs/" S6RC_FDHOLDER "/s")) ;
  if (!s6_fdholder_start_g(&a, fnsocket, deadline)) goto hammer ;
  if (!delete_unused_pipes(&a, olddb, oldstate, deadline)) goto freehammer ;
  if (!rename_pipes(&a, olddb, newdb, newstate, invimage, deadline)) goto freehammer ;
  if (!create_new_pipes(&a, newdb, newstate, deadline)) goto freehammer ;
  s6_fdholder_end(&a) ;
  return ;

 freehammer:
  s6_fdholder_end(&a) ;
 hammer:
  if (verbosity) strerr_warnwu1x("live update s6rc-fdholder - restarting it") ;
  {
    pid_t pid ;
    int wstat ;
    char tfmt[UINT_FMT] ;
    char const *newargv[7] = { S6_EXTBINPREFIX "s6-svc", "-T", tfmt, "-twR", "--", fnsocket, 0 } ;
    fill_tfmt(tfmt, deadline) ;
    fnsocket[livelen + sizeof("/servicedirs/" S6RC_FDHOLDER) - 1] = 0 ;
    pid = child_spawn0(newargv[0], newargv, envp) ;
    if (!pid) strerr_diefu2sys(111, "spawn ", newargv[0]) ;
    if (wait_pid(pid, &wstat) < 0) strerr_diefu1sys(111, "waitpid") ;
    tain_now_g() ;
    if (WIFSIGNALED(wstat) || WEXITSTATUS(wstat))
      if (verbosity) strerr_warnwu1x("restart s6rc-fdholder") ;
    if (!tain_future(deadline)) strerr_dief1x(2, "timed out during s6rc-fdholder update") ;
  }
}



 /* Main */


static unsigned int want_count (unsigned char const *state, unsigned int n)
{
  unsigned int count = 0, i = n ;
  while (i--) if (state[i] & 2) count++ ;
  return count ;
}

int main (int argc, char const *const *argv, char const *const *envp)
{
  char const *convfile = "/dev/null" ;
  tain_t deadline ;
  int dryrun = 0 ;
  int blocking = 0 ;
  PROG = "s6-rc-update" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "v:t:nl:f:b", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 't' : if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        case 'n' : dryrun = 1 ; break ;
        case 'l' : live = l.arg ; break ;
        case 'f' : convfile = l.arg ; break ;
        case 'b' : blocking = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&deadline, t) ;
    else deadline = tain_infinite_relative ;
  }
  if (!argc) dieusage() ;
  if (argv[0][0] != '/')
    strerr_dief2x(100, argv[0], " is not an absolute path") ;
  livelen = strlen(live) ;

  {
    int livelock, oldlock, newlock ;
    int fdoldc, fdnewc ;
    s6rc_db_t olddb, newdb ;
    unsigned int oldn, newn ;
    size_t prefixlen = 0 ;
    char dbfn[livelen + 10] ;

    tain_now_g() ;
    tain_add_g(&deadline, &deadline) ;


   /* Take the live, old and new locks */

    memcpy(dbfn, live, livelen) ;
    memcpy(dbfn + livelen, "/compiled", 10) ;
    if (!s6rc_lock(live, 2, &livelock, dbfn, 1, &oldlock, blocking))
      strerr_diefu4sys(111, "take lock on ", live, " and ", dbfn) ;
    if (!s6rc_lock(0, 0, 0, argv[0], 1, &newlock, blocking))
      strerr_diefu2sys(111, "take lock on ", argv[0]) ;


   /* Read the sizes of the prefix and compiled dbs */

    if (!s6rc_livedir_prefixsize(live, &prefixlen))
      strerr_diefu2sys(111, "read prefix size for ", live) ;
    fdoldc = open_readb(dbfn) ;
    if (!s6rc_db_read_sizes(fdoldc, &olddb))
      strerr_diefu3sys(111, "read ", dbfn, "/n") ;
    oldn = olddb.nshort + olddb.nlong ;
    fdnewc = open_readb(argv[0]) ;
    if (!s6rc_db_read_sizes(fdnewc, &newdb))
      strerr_diefu3sys(111, "read ", argv[0], "/n") ;
    newn = newdb.nshort + newdb.nlong ;


   /* Allocate enough stack for the dbs */

    {
      pid_t pid ;
      stralloc sa = STRALLOC_ZERO ;
      s6rc_service_t oldserviceblob[oldn] ;
      char const *oldargvblob[olddb.nargvs] ;
      uint32_t olddepsblob[olddb.ndeps << 1] ;
      uint32_t oldproducersblob[olddb.nproducers] ;
      s6rc_service_t newserviceblob[newn] ;
      char const *newargvblob[newdb.nargvs] ;
      uint32_t newdepsblob[newdb.ndeps << 1] ;
      uint32_t newproducersblob[newdb.nproducers] ;
      unsigned int invimage[newn] ;
      char oldstringblob[olddb.stringlen] ;
      char newstringblob[newdb.stringlen] ;
      unsigned char oldstate[oldn] ;
      unsigned char newstate[newn] ;
      char prefix[prefixlen + 1] ;
      int r ;

      olddb.services = oldserviceblob ;
      olddb.argvs = oldargvblob ;
      olddb.deps = olddepsblob ;
      olddb.producers = oldproducersblob ;
      olddb.string = oldstringblob ;
      newdb.services = newserviceblob ;
      newdb.argvs = newargvblob ;
      newdb.deps = newdepsblob ;
      newdb.producers = newproducersblob ;
      newdb.string = newstringblob ;


     /* Read the prefix */

      {
        ssize_t rr = s6rc_livedir_prefix(live, prefix, prefixlen) ;
        if (rr != prefixlen) strerr_diefu2sys(111, "read prefix for ", live) ;
        prefix[prefixlen] = 0 ;
      }


     /* Read the dbs */

      r = s6rc_db_read(fdoldc, &olddb) ;
      if (r < 0) strerr_diefu3sys(111, "read ", dbfn, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", dbfn, "/db") ;
      r = s6rc_db_read(fdnewc, &newdb) ;
      if (r < 0) strerr_diefu3sys(111, "read ", argv[0], "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", argv[0], "/db") ;


     /* Initial state */

      memcpy(dbfn + livelen + 1, "state", 6) ;
      {
        ssize_t rr = openreadnclose(dbfn, (char *)oldstate, oldn) ;
        if (rr != oldn) strerr_diefu2sys(111, "read ", dbfn) ;
      }
      r = oldn ;
      while (r--) oldstate[r] &= 1 ;
      memset(newstate, 0, newn) ;
      r = newn ;
      while (r--) invimage[r] = olddb.nlong + olddb.nshort ;


     /* Read the conversion file and compute what to do */

      if (verbosity >= 2) strerr_warni1x("computing state adjustments") ;
      compute_transitions(convfile, oldstate, fdoldc, &olddb, newstate, invimage, fdnewc, argv[0], &newdb, &sa) ;
      tain_now_g() ;
      if (!tain_future(&deadline)) strerr_dief1x(10, "timed out while computing state adjutments") ;


     /* Down transition */

      {
        char const *newargv[12 + (dryrun * 4) + want_count(oldstate, oldn)] ;
        unsigned int m = 0, i = oldn ;
        int wstat ;
        char vfmt[UINT_FMT] ;
        char tfmt[UINT_FMT] ;
        vfmt[uint_fmt(vfmt, verbosity)] = 0 ;
        fill_tfmt(tfmt, &deadline) ;
        if (dryrun)
        {
          newargv[m++] = S6RC_BINPREFIX "s6-rc-dryrun" ;
          newargv[m++] = "-v" ;
          newargv[m++] = vfmt ;
          newargv[m++] = "-t0" ;
          newargv[m++] = "--" ;
        }
        newargv[m++] = S6RC_BINPREFIX "s6-rc" ;
        newargv[m++] = "-v" ;
        newargv[m++] = vfmt ;
        newargv[m++] = "-t" ;
        newargv[m++] = tfmt ;
        newargv[m++] = "-l" ;
        newargv[m++] = live ;
        if (!dryrun) newargv[m++] = "-X" ;
        newargv[m++] = "-d" ;
        newargv[m++] = "--" ;
        newargv[m++] = "change" ;
        while (i--) if (oldstate[i] & 2)
          newargv[m++] = olddb.string + olddb.services[i].name ;
        newargv[m++] = 0 ;
        if (verbosity >= 2)
          strerr_warni1x("stopping services in the old database") ;
        pid = child_spawn0(newargv[0], newargv, envp) ;
        if (!pid) strerr_diefu2sys(111, "spawn ", newargv[0]) ;
        if (wait_pid(pid, &wstat) < 0) strerr_diefu1sys(111, "waitpid") ;
        tain_now_g() ;
        if (WIFSIGNALED(wstat) || WEXITSTATUS(wstat))
        {
          wstat = wait_estatus(wstat) ;
          if (wstat == 1 || wstat == 2) wstat += 8 ;
          strerr_dief1x(wstat, "first s6-rc invocation failed") ;
        }
      }

      if (!dryrun)
      {
       /* Update state and service directories */

        if (verbosity >= 2)
          strerr_warni1x("updating state and service directories") ;

        make_new_livedir(oldstate, &olddb, newstate, &newdb, argv[0], invimage, prefix, &sa) ;
        stralloc_free(&sa) ;
        r = s6rc_servicedir_manage_g(live, prefix, &deadline) ;
        if (!r) strerr_diefu2sys(111, "manage new service directories in ", live) ;
        if (r & 2) strerr_warnw3x("s6-svscan not running on ", live, "/scandir") ;

       /* Adjust stored pipes */

        if (verbosity >= 2)
          strerr_warni1x("updating s6rc-fdholder pipe storage") ;

        update_fdholder(&olddb, oldstate, &newdb, newstate, invimage, envp, &deadline) ;
      }


     /* Up transition */

      {
        char const *newargv[12 + (dryrun * 4) + want_count(newstate, newn)] ;
        unsigned int m = 0, i = newn ;
        char vfmt[UINT_FMT] ;
        char tfmt[UINT_FMT] ;
        vfmt[uint_fmt(vfmt, verbosity)] = 0 ;
        fill_tfmt(tfmt, &deadline) ;
        if (dryrun)
        {
          newargv[m++] = S6RC_BINPREFIX "s6-rc-dryrun" ;
          newargv[m++] = "-v" ;
          newargv[m++] = vfmt ;
          newargv[m++] = "-t0" ;
          newargv[m++] = "--" ;
        }
        newargv[m++] = S6RC_BINPREFIX "s6-rc" ;
        newargv[m++] = "-v" ;
        newargv[m++] = vfmt ;
        newargv[m++] = "-t" ;
        newargv[m++] = tfmt ;
        newargv[m++] = "-l" ;
        newargv[m++] = live ;
        if (!dryrun) newargv[m++] = "-X" ;
        newargv[m++] = "-u" ;
        newargv[m++] = "--" ;
        newargv[m++] = "change" ;
        while (i--) if (newstate[i] & 2)
          newargv[m++] = newdb.string + newdb.services[i].name ;
        newargv[m++] = 0 ;
        if (verbosity >= 2)
          strerr_warni1x("starting services in the new database") ;
        xpathexec_run(newargv[0], newargv, envp) ;
      }
    }
  }
}
