/* ISC license. */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <skalibs/uint32.h>
#include <skalibs/uint.h>
#include <skalibs/bytestr.h>
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
#include <skalibs/webipc.h>
#include <skalibs/unix-transactional.h>
#include <skalibs/random.h>
#include <execline/execline.h>
#include <s6/config.h>
#include <s6/s6-supervise.h>
#include <s6/s6-fdholder.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-update [ -n ] [ -v verbosity ] [ -t timeout ] [ -l live ] [ -f conversion_file ] newdb"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "build string") ;

static char const *live = S6RC_LIVE_BASE ;
static unsigned int livelen = sizeof(S6RC_LIVE_BASE) - 1 ;
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

static inline void parse_line (stralloc *sa, char const *s, unsigned int slen, unsigned int *newnames, unsigned char *oldstate, cdb_t *oldc, s6rc_db_t const *olddb)
{
  unsigned int base = sa->len ;
  unsigned int oldn = olddb->nshort + olddb->nlong ;
  unsigned int max ;
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
  max = sa->len ;
  sa->len = base ;
  if (n--)
  {
    char pack[4] ;
    uint32 x ;
    unsigned int cur ;
    register int r = cdb_find(oldc, sa->s + base + slen, str_len(sa->s + base + slen)) ;
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
    cur = base + slen + str_len(sa->s + base + slen) + 1 ;
    if (n >= 2 && !str_diff(sa->s + cur, "->"))
    {
      register unsigned int newnamelen = str_len(sa->s + cur + 3) ;
      byte_copy(sa->s + sa->len, newnamelen + 1, sa->s + cur + 3) ;
      newnames[x] = sa->len ; oldstate[x] |= 16 ;
      sa->len += newnamelen + 1 ;
      cur += newnamelen + 4 ;
      n -= 2 ;
    }
    while (n--)
    {
      if (!str_diff(sa->s + cur, "restart")) oldstate[x] |= 4 ;
      else
        strerr_dief2x(100, "unknown keyword in conversion file: ", sa->s + cur) ;
      cur += str_len(sa->s + cur) + 1 ;
    }
  }
}

static inline void parse_conversion_file (char const *convfile, stralloc *sa, unsigned int *newnames, unsigned char *oldstate, cdb_t *oldc, s6rc_db_t const *olddb)
{
  int fd = open_readb(convfile) ;
  char buf[4096] ;
  buffer b = BUFFER_INIT(&fd_readsv, fd, buf, 4096) ;
  unsigned int base = satmp.len ;
  if (fd < 0) strerr_diefu2sys(111, "open ", convfile) ;

  for (;;)
  {
    register int r = skagetln(&b, &satmp, '\n') ;
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
    unsigned int len ;
    int r = cdb_find(newc, newname, str_len(newname)) ;
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
        uint32 x ;
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
  unsigned int sabase = sa->len ;
  unsigned char conversion_table[oldn * newm] ;
  byte_zero(conversion_table, oldn * newm) ;
  stuff_with_oldc(oldstate, fdoldc, olddb, convfile, oldindex, sa) ;
  stuff_with_newc(fdnewc, newfn, conversion_table, oldstate, newstate, sa->s + sabase, oldindex, invimage, olddb, newdb) ;
  sa->len = sabase ;

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
      register unsigned int j = newn ;
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
  unsigned int newllen = str_len(newlive) ;
  unsigned int i = n ;
  while (i--)
  {
    unsigned int newnamelen = str_len(newdb->string + newdb->services[i].name) ;
    char newfn[newllen + 14 + newnamelen] ;
    byte_copy(newfn, newllen, newlive) ;
    byte_copy(newfn + newllen, 13, "/servicedirs/") ;
    byte_copy(newfn + newllen + 13, newnamelen + 1, newdb->string + newdb->services[i].name) ;
    if (newstate[i] & 1)
    {
      char const *oldname = newstate[i] & 8 ? olddb->string + olddb->services[invimage[i]].name : newdb->string + newdb->services[i].name ;
      unsigned int oldnamelen = str_len(oldname) ;
      char oldfn[livelen + 23 + oldnamelen] ;
      byte_copy(oldfn, livelen, live) ;
      byte_copy(oldfn + livelen, 22, "/compiled/servicedirs/") ;
      byte_copy(oldfn + livelen + 22, oldnamelen + 1, oldname) ;
      if (!s6rc_servicedir_copy_online(oldfn, newfn))
        strerr_diefu4sys(111, "rollback ", oldfn, " into ", newfn) ;
      byte_copy(oldfn + livelen, 13, "/servicedirs/") ;
      byte_copy(oldfn + livelen + 13, oldnamelen + 1, oldname) ;
      if (rename(newfn, oldfn) < 0)
        strerr_diefu4sys(111, "rollback: can't rename ", newfn, " to ", oldfn) ;
    }
  }
}

static inline void unsupervise (char const *llive, char const *name, int keepsupervisor)
{
  unsigned int namelen = str_len(name) ;
  unsigned int llen = str_len(llive) ;
  char fn[llen + 14 + namelen] ;
  byte_copy(fn, llen, llive) ;
  byte_copy(fn + llen, 9, "/scandir/") ;
  byte_copy(fn + llen + 9, namelen + 1, name) ;
  unlink(fn) ;
  if (!keepsupervisor)
  {
    byte_copy(fn + llen + 1, 12, "servicedirs/") ;
    byte_copy(fn + llen + 13, namelen + 1, name) ;
    s6_svc_writectl(fn, S6_SUPERVISE_CTLDIR, "x", 1) ;
  }
}

static inline void make_new_livedir (unsigned char const *oldstate, s6rc_db_t const *olddb, unsigned char const *newstate, s6rc_db_t const *newdb, char const *newcompiled, unsigned int *invimage, stralloc *sa)
{
  unsigned int tmpbase = satmp.len ;
  unsigned int sabase = sa->len ;
  unsigned int newclen = str_len(newcompiled) ;
  unsigned int dirlen, llen, newlen, sdlen ;
  int e = 0 ;
  unsigned int i = 0 ;
  if (sareadlink(&satmp, live) < 0) strerr_diefu2sys(111, "readlink ", live) ;
  if (!s6rc_sanitize_dir(sa, live, &dirlen)) dienomem() ;
  llen = sa->len ;
  if (!random_sauniquename(sa, 8) || !stralloc_0(sa)) dienomem() ;
  newlen = --sa->len ;
  if (mkdir(sa->s + sabase, 0755) < 0) strerr_diefu2sys(111, "mkdir ", sa->s + sabase) ;
  {
    unsigned int tmplen = satmp.len ;
    char fn[llen - sabase + 9] ;
    if (!stralloc_cats(sa, "/scandir") || !stralloc_0(sa)) { e = errno ; goto err ; }
    byte_copy(fn, llen - sabase, sa->s + sabase) ;
    byte_copy(fn + llen - sabase, 9, "/scandir") ;
    if (sareadlink(&satmp, fn) < 0 || !stralloc_0(&satmp)) { e = errno ; goto err ; }
    if (symlink(satmp.s + tmplen, sa->s + sabase) < 0) { e = errno ; goto err ; }
    satmp.len = tmplen ;
    sa->len = newlen ;
  }
  if (!stralloc_cats(sa, "/state") || !stralloc_0(sa)) { e = errno ; goto err ; }
  {
    char tmpstate[newdb->nlong + newdb->nshort] ;
    unsigned int i = newdb->nlong + newdb->nshort ;
    while (i--) tmpstate[i] = newstate[i] & 1 ;
    if (!openwritenclose_unsafe(sa->s + sabase, tmpstate, newdb->nlong + newdb->nshort)) { e = errno ; goto err ; }
  }
  sa->len = newlen ;
  if (!stralloc_cats(sa, "/compiled") || !stralloc_0(sa)) { e = errno ; goto err ; }
  if (symlink(newcompiled, sa->s + sabase) < 0) goto err ;
  sa->len = newlen ;
  if (!stralloc_cats(sa, "/servicedirs") || !stralloc_0(sa)) { e = errno ; goto err ; }
  if (mkdir(sa->s + sabase, 0755) < 0) { e = errno ; goto err ; }
  sdlen = sa->len ;
  sa->s[sdlen - 1] = '/' ;

  for (; i < newdb->nlong ; i++)
  {
    unsigned int newnamelen = str_len(newdb->string + newdb->services[i].name) ;
    char newfn[newclen + 14 + newnamelen] ;
    byte_copy(newfn, newclen, newcompiled) ;
    byte_copy(newfn + newclen, 13, "/servicedirs/") ;
    byte_copy(newfn + newclen + 13, newnamelen + 1, newdb->string + newdb->services[i].name) ;
    sa->len = sdlen ;
    if (!stralloc_cats(sa, newdb->string + newdb->services[i].name)
     || !stralloc_0(sa)) { e = errno ; goto rollback ; }
    if (newstate[i] & 1)
    {
      char const *oldname = newstate[i] & 8 ? olddb->string + olddb->services[invimage[i]].name : newdb->string + newdb->services[i].name ;
      unsigned int oldnamelen = str_len(oldname) ;
      char oldfn[livelen + 14 + oldnamelen] ;
      byte_copy(oldfn, livelen, live) ;
      byte_copy(oldfn + livelen, 13, "/servicedirs/") ;
      byte_copy(oldfn + livelen + 13, oldnamelen + 1, oldname) ;
      if (rename(oldfn, sa->s + sabase) < 0) goto rollback ;
      if (!s6rc_servicedir_copy_online(newfn, sa->s + sabase)) { i++ ; e = errno ; goto rollback ; }
    }
    else if (!s6rc_servicedir_copy_offline(newfn, sa->s + sabase)) { e = errno ; goto rollback ; }
  }

  sa->len = newlen ;
  sa->s[sa->len++] = 0 ;
  {
    char tmpfn[llen + 5 - sabase] ;
    byte_copy(tmpfn, llen - sabase, sa->s + sabase) ;
    byte_copy(tmpfn + llen - sabase, 5, ".new") ;
    if (unlink(tmpfn) < 0 && errno != ENOENT) { e = errno ; goto rollback ; }
    if (symlink(sa->s + dirlen, tmpfn) < 0) { e = errno ; goto rollback ; }

   /* The point of no return is here */
    if (rename(tmpfn, live) < 0)
    {
      e = errno ;
      unlink(tmpfn) ;
      goto rollback ;
    }
  }

  if (verbosity >= 2)
    strerr_warni1x("successfully switched to new database") ;

 /* scandir cleanup, then old livedir cleanup */
  sa->len = dirlen ;
  if (!stralloc_catb(sa, satmp.s + tmpbase, satmp.len - tmpbase) || !stralloc_0(sa))
    dienomem() ;
  i = olddb->nlong ;
  while (i--) unsupervise(sa->s + sabase, olddb->string + olddb->services[i].name, (oldstate[i] & 33) == 1) ;
  rm_rf(sa->s + sabase) ;

  sa->len = sabase ;
  satmp.len = tmpbase ;
  return ;

 rollback:
  sa->len = newlen ;
  sa->s[sa->len++] = 0 ;
  rollback_servicedirs(sa->s + sabase, newstate, invimage, olddb, newdb, i) ;
 err:
  sa->len = newlen ;
  sa->s[sa->len++] = 0 ;
  rm_rf(sa->s + sabase) ;
  errno = e ;
  strerr_diefu2sys(111, "make new live directory in ", sa->s) ;
}


 /* Updating the pipes contained in the fdholder */

static inline int delete_unused_pipes (s6_fdholder_t *a, s6rc_db_t const *olddb, unsigned char const *oldstate, tain_t const *deadline)
{
  unsigned int i = olddb->nlong ;
  while (i--)
    if (!(oldstate[i] & 8)
     && olddb->services[i].x.longrun.pipeline[0] < olddb->nlong)
    {
      unsigned int len = str_len(olddb->string + olddb->services[i].name) ;
      char pipename[len + 13] ;
      byte_copy(pipename, 12, "pipe:s6rc-w-") ;
      byte_copy(pipename + 12, len + 1, olddb->string + olddb->services[i].name) ;
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
    if ((newstate[i] & 20) == 20 && newdb->services[i].x.longrun.pipeline[0] < newdb->nlong)
    {
      int fd ;
      unsigned int oldlen = str_len(olddb->string + olddb->services[invimage[i]].name) ;
      unsigned int newlen = str_len(newdb->string + newdb->services[i].name) ;
      char oldpipename[oldlen + 13] ;
      char newpipename[newlen + 13] ;
      byte_copy(oldpipename, 12, "pipe:s6rc-r-") ;
      byte_copy(oldpipename + 12, oldlen + 1, olddb->string + olddb->services[invimage[i]].name) ;
      byte_copy(newpipename, 12, "pipe:s6rc-r-") ;
      byte_copy(newpipename + 12, newlen + 1, newdb->string + newdb->services[i].name) ;
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
    if (!(newstate[i] & 4) && newdb->services[i].x.longrun.pipeline[0] < newdb->nlong)
    {
      int p[2] ;
      unsigned int len = str_len(newdb->string + newdb->services[i].name) ;
      char pipename[len + 13] ;
      byte_copy(pipename, 12, "pipe:s6rc-r-") ;
      byte_copy(pipename + 12, len + 1, newdb->string + newdb->services[i].name) ;
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
  int fdsocket ;
  s6_fdholder_t a = S6_FDHOLDER_ZERO ;
  char fnsocket[livelen + sizeof("/servicedirs/" S6RC_FDHOLDER "/s")] ;
  if (!(newstate[1] & 1)) return ;
  byte_copy(fnsocket, livelen, live) ;
  byte_copy(fnsocket + livelen, sizeof("/servicedirs/" S6RC_FDHOLDER "/s"), "/servicedirs/" S6RC_FDHOLDER "/s") ;
  fdsocket = ipc_stream_nb() ;
  if (fdsocket < 0) goto hammer ;
  if (!ipc_timed_connect_g(fdsocket, fnsocket, deadline))
  {
    if (errno == ETIMEDOUT) strerr_dief1x(2, "timed out during s6rc-fdholder update") ;
    else goto closehammer ;
  }
  s6_fdholder_init(&a, fdsocket) ;
  if (!delete_unused_pipes(&a, olddb, oldstate, deadline)) goto freehammer ;
  if (!rename_pipes(&a, olddb, newdb, newstate, invimage, deadline)) goto freehammer ;
  if (!create_new_pipes(&a, newdb, newstate, deadline)) goto freehammer ;
  s6_fdholder_free(&a) ;
  close(fdsocket) ;
  return ;

 freehammer:
  s6_fdholder_free(&a) ;
 closehammer:
  close(fdsocket) ;
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
  PROG = "s6-rc-update" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "v:t:nl:f:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 't' : if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        case 'n' : dryrun = 1 ; break ;
        case 'l' : live = l.arg ; break ;
        case 'f' : convfile = l.arg ; break ;
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
  if (live[0] != '/')
    strerr_dief2x(100, live, " is not an absolute path") ;
  livelen = str_len(live) ;
  {
    int livelock, oldlock, newlock ;
    int fdoldc, fdnewc ;
    s6rc_db_t olddb, newdb ;
    unsigned int oldn, newn ;
    char dbfn[livelen + 10] ;

    if (!tain_now_g())
      strerr_warnwu1x("get correct TAI time. (Do you have a valid leap seconds file?)") ;
    tain_add_g(&deadline, &deadline) ;


   /* Take the live, old and new locks */

    byte_copy(dbfn, livelen, live) ;
    byte_copy(dbfn + livelen, 10, "/compiled") ;
    if (!s6rc_lock(live, 2, &livelock, dbfn, 1, &oldlock))
      strerr_diefu4sys(111, "take lock on ", live, " and ", dbfn) ;
    if (!s6rc_lock(0, 0, 0, argv[0], 1, &newlock))
      strerr_diefu2sys(111, "take lock on ", argv[0]) ;


   /* Read the sizes of the compiled dbs */

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
      uint32 olddepsblob[olddb.ndeps << 1] ;
      s6rc_service_t newserviceblob[newn] ;
      char const *newargvblob[newdb.nargvs] ;
      uint32 newdepsblob[newdb.ndeps << 1] ;
      unsigned int invimage[newn] ;
      char oldstringblob[olddb.stringlen] ;
      char newstringblob[newdb.stringlen] ;
      unsigned char oldstate[oldn] ;
      unsigned char newstate[newn] ;
      register int r ;

      olddb.services = oldserviceblob ;
      olddb.argvs = oldargvblob ;
      olddb.deps = olddepsblob ;
      olddb.string = oldstringblob ;
      newdb.services = newserviceblob ;
      newdb.argvs = newargvblob ;
      newdb.deps = newdepsblob ;
      newdb.string = newstringblob ;


     /* Read the dbs */

      r = s6rc_db_read(fdoldc, &olddb) ;
      if (r < 0) strerr_diefu3sys(111, "read ", dbfn, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", dbfn, "/db") ;
      r = s6rc_db_read(fdnewc, &newdb) ;
      if (r < 0) strerr_diefu3sys(111, "read ", argv[0], "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", argv[0], "/db") ;


     /* Initial state */

      byte_copy(dbfn + livelen + 1, 6, "state") ;
      r = openreadnclose(dbfn, (char *)oldstate, oldn) ;
      if (r != oldn) strerr_diefu2sys(111, "read ", dbfn) ;
      r = oldn ;
      while (r--) oldstate[r] &= 1 ;
      byte_zero(newstate, newn) ;
      r = newn ;
      while (r--) invimage[r] = olddb.nlong + olddb.nshort ;


     /* Read the conversion file and compute what to do */

      if (verbosity >= 2) strerr_warni1x("computing state adjustments") ;
      compute_transitions(convfile, oldstate, fdoldc, &olddb, newstate, invimage, fdnewc, argv[0], &newdb, &sa) ;
      tain_now_g() ;
      if (!tain_future(&deadline)) strerr_dief1x(2, "timed out while computing state adjutments") ;


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
          strerr_dief1x(wait_estatus(wstat), "first s6-rc invocation failed") ;
      }

      if (!dryrun)
      {
        int r ;

       /* Update state and service directories */

        if (verbosity >= 2)
          strerr_warni1x("updating state and service directories") ;

        make_new_livedir(oldstate, &olddb, newstate, &newdb, argv[0], invimage, &sa) ;
        r = s6rc_servicedir_manage_g(live, &deadline) ;
        if (!r) strerr_diefu2sys(111, "manage new service directories in ", live) ;
        if (r & 2) strerr_warnw3x("s6-svscan not running on ", live, "/scandir") ;


       /* Adjust stored pipelines */

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
        pathexec_run(newargv[0], newargv, envp) ;
        strerr_dieexec(111, newargv[0]) ;
      }
    }
  }
}
