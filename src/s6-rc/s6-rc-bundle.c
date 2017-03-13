/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <skalibs/uint32.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/bitarray.h>
#include <skalibs/djbunix.h>
#include <skalibs/cdb.h>
#include <skalibs/cdb_make.h>
#include <skalibs/unix-transactional.h>
#include <execline/execline.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-bundle [ -l live ] [ -c compiled ] command... (use s6-rc-bundle help for more information)"
#define dieusage() strerr_dieusage(100, USAGE)

static void cleanup (char const *compiled)
{
  size_t len = strlen(compiled) ;
  int e = errno ;
  char fn[len + sizeof("/resolve.cdb.new")] ;
  memcpy(fn, compiled, len) ;
  memcpy(fn + len, "/resolve.cdb.new", sizeof("/resolve.cdb.new")) ;
  unlink(fn) ;
  errno = e ;
}


 /* switch to renameat when it's more portable */

static inline int renameit (char const *compiled, char const *src, char const *dst)
{
  size_t clen = strlen(compiled) ;
  size_t srclen = strlen(src) ;
  size_t dstlen = strlen(dst) ;
  char srcfn[clen + srclen + 2] ;
  char dstfn[clen + dstlen + 2] ;
  memcpy(srcfn, compiled, clen) ;
  srcfn[clen] = '/' ;
  memcpy(srcfn + clen + 1, src, srclen + 1) ;
  memcpy(dstfn, compiled, clen) ;
  dstfn[clen] = '/' ;
  memcpy(dstfn + clen + 1, dst, dstlen + 1) ;
  return rename(srcfn, dstfn) ;
}

static void check (cdb_t *cr, s6rc_db_t *db, char const *name, int h, int force, char const *compiled)
{
  size_t namelen = strlen(name) ;
  int r = cdb_find(cr, name, namelen) ;
  if (r < 0) strerr_diefu3sys(111, "cdb_find in ", compiled, "/resolve.cdb") ;
  if (!r)
  {
    if (!h && !force)
      strerr_dief4x(3, "identifier ", name, " does not exist in database ", compiled) ;
    return ;
  }
  if (h && !force)
    strerr_dief4x(1, "identifier ", name, " already exists in database ", compiled) ;
  if (cdb_datalen(cr) == 4)
  {
    uint32_t x ;
    char pack[4] ;
    if (cdb_read(cr, pack, 4, cdb_datapos(cr)) < 0)
      strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
    uint32_unpack_big(pack, &x) ;
    if (x >= db->nshort + db->nlong)
      strerr_dief2x(4, "invalid database in ", compiled) ;
    if (!strcmp(name, db->string + db->services[x].name))
      strerr_dief4x(5, "identifier ", name, " does not represent a bundle for database ", compiled) ;
  }
}

static void modify_resolve (int fdcompiled, s6rc_db_t *db, char const *const *todel, unsigned int todeln, char const *const *toadd, char const *const *const *toadd_contents, unsigned int toaddn, int force, char const *compiled)
{
  cdb_t cr = CDB_ZERO ;
  struct cdb_make cw = CDB_MAKE_ZERO ;
  uint32_t kpos ;
  unsigned int i = toaddn ;
  int fdw ;
  int fdr = open_readatb(fdcompiled, "resolve.cdb") ;
  unsigned int n = db->nlong + db->nshort ;
  unsigned char bits[bitarray_div8(n)] ;
  if (fdr < 0) strerr_diefu3sys(111, "open ", compiled, "/resolve.cdb") ;
  if (!cdb_init_map(&cr, fdr, 1))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  while (i--) check(&cr, db, toadd[i], 1, force, compiled) ;
  i = todeln ;
  while (i--) check(&cr, db, todel[i], 0, force, compiled) ;
  fdw = open_truncatb(fdcompiled, "resolve.cdb.new") ;
  if (fdw < 0) strerr_diefu3sys(111, "open ", compiled, "/resolve.cdb.new") ;
  if (cdb_make_start(&cw, fdw) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu1sys(111, "cdb_make_start") ;
  }
  cdb_traverse_init(&cr, &kpos) ;
  for (;;)
  {
    int r = cdb_nextkey(&cr, &kpos) ;
    if (r < 0)
    {
      cleanup(compiled) ;
      strerr_diefu3sys(111, "cdb_nextkey in ", compiled, "/resolve.cdb") ;
    }
    if (!r) break ;
    {
      uint32_t klen = cdb_keylen(&cr) ;
      char ktmp[klen + 1] ;
      if (cdb_read(&cr, ktmp, klen, cdb_keypos(&cr)) < 0)
      {
        cleanup(compiled) ;
        strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
      }
      for (i = 0 ; i < todeln ; i++) if (!strncmp(todel[i], ktmp, klen)) break ;
      if (i < todeln) continue ;
      for (i = 0 ; i < toaddn ; i++) if (!strncmp(toadd[i], ktmp, klen)) break ;
      if (i < toaddn) continue ;
      {
        uint32_t dlen = cdb_datalen(&cr) ;
        char dtmp[dlen + 1] ;
        if (cdb_read(&cr, dtmp, dlen, cdb_datapos(&cr)) < 0)
        {
          cleanup(compiled) ;
          strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
        }
        if (cdb_make_add(&cw, ktmp, klen, dtmp, dlen) < 0)
        {
          cleanup(compiled) ;
          strerr_diefu1sys(111, "cdb_make_add") ;
        }
      }
    }
  }
  i = toaddn ;
  while (i--)
  {
    char const *const *p = toadd_contents[i] ;
    unsigned int total = 0 ;
    memset(bits, 0, bitarray_div8(n)) ;
    for (; *p ; p++)
    {
      int r = cdb_find(&cr, *p, strlen(*p)) ;
      if (r < 0) strerr_diefu3sys(111, "cdb_find in ", compiled, "/resolve.cdb") ;
      if (!r) strerr_dief4x(3, "identifier ", *p, " does not exist in database ", compiled) ;
      {
        uint32_t j = cdb_datalen(&cr) ;
        char pack[j + 1] ;
        if (cdb_read(&cr, pack, j, cdb_datapos(&cr)) < 0)
        {
          cleanup(compiled) ;
          strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
        }
        j >>= 2 ;
        while (j--)
        {
          uint32_t x ;
          uint32_unpack_big(pack + (j << 2), &x) ;
          if (x >= db->nshort + db->nlong)
            strerr_dief2x(4, "invalid database in ", compiled) ;
          if (!bitarray_testandset(bits, x)) total++ ;
        }
      }
    }
    {
      char pack[(total << 2) + 1] ;
      char *s = pack ;
      uint32_t j = n ;
      while (j--) if (bitarray_peek(bits, j))
      {
        uint32_pack_big(s, j) ;
        s += 4 ;
      }
      if (cdb_make_add(&cw, toadd[i], strlen(toadd[i]), pack, total << 2) < 0)
      {
        cleanup(compiled) ;
        strerr_diefu1sys(111, "cdb_make_add") ;
      }
    }
  }
  cdb_free(&cr) ;
  close(fdr) ;
  if (cdb_make_finish(&cw) < 0 || fsync(fdw) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu3sys(111, "write to ", compiled, "/resolve.cdb.new") ;
  }
  close(fdw) ;
  if (renameit(compiled, "resolve.cdb.new", "resolve.cdb") < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "rename resolve.cdb.new to resolve.cdb in ", compiled) ;
  }
}

static inline void parse_multiple (int argc, char const **argv, unsigned int *todeln, unsigned int *toaddn, char const **toadd, char const *const **toadd_contents)
{
  unsigned int m = 0 ;
  int argc1 = el_semicolon(argv) ;
  if (argc1 >= argc) strerr_dief1x(100, "unterminated block") ;
  *todeln = argc1 ;
  argv += argc1 + 1 ; argc -= argc1 + 1 ;
  while (argc)
  {
    toadd[m] = *argv++ ;
    if (!--argc) strerr_dief1x(100, "missing bundle contents block") ;
    argc1 = el_semicolon(argv) ;
    if (argc1 >= argc) strerr_dief1x(100, "unterminated block") ;
    argv[argc1] = 0 ;
    toadd_contents[m++] = argv ;
    argv += argc1 + 1 ; argc -= argc1 + 1 ;
  }
  *toaddn = m ;
}

static inline void print_help (void)
{
  static char const *help =
"s6-rc-bundle help\n"
"s6-rc-bundle add bundlename contents...\n"
"s6-rc-bundle delete bundlenames...\n"
"s6-rc-bundle multiple { to-delete } to-add { contents... } ... (execlineb syntax)\n" ;
  if (buffer_putsflush(buffer_1, help) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static inline unsigned int lookup (char const *const *table, char const *command)
{
  unsigned int i = 0 ;
  for (; table[i] ; i++) if (!strcmp(command, table[i])) break ;
  return i ;
}

static inline unsigned int parse_command (char const *command)
{
  static char const *const command_table[5] =
  {
    "help",
    "add",
    "delete",
    "multiple",
    0
  } ;
  unsigned int i = lookup(command_table, command) ;
  if (!command_table[i]) dieusage() ;
  return i ;
}

int main (int argc, char const **argv)
{
  char const *live = S6RC_LIVE_BASE ;
  char const *compiled = 0 ;
  unsigned int what ;
  int force = 0 ;
  PROG = "s6-rc-bundle" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "fl:c:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'f' : force = 1 ; break ;
        case 'l' : live = l.arg ; break ;
        case 'c' : compiled = l.arg ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (!argc) dieusage() ;
  what = parse_command(*argv++) ;
  if (!what)
  {
    print_help() ;
    return 0 ;
  }
  if (!--argc) dieusage() ;
  if (what != 2 && argc < 2) dieusage() ;

  {
    size_t livelen = strlen(live) ;
    int fdcompiled = -1 ;
    int compiledlock ;
    s6rc_db_t dbblob ;
    char compiledblob[compiled ? strlen(compiled) : livelen + 10] ;

    if (!compiled)
    {
      memcpy(compiledblob, live, livelen) ;
      memcpy(compiledblob + livelen, "/compiled", 10) ;
      compiled = compiledblob ;
    }

    if (!s6rc_lock(0, 0, 0, compiled, 2, &compiledlock))
      strerr_diefu2sys(111, "take lock on ", compiled) ;
    fdcompiled = open_readb(compiled) ;
    if (fdcompiled < 0)
      strerr_diefu2sys(111, "open ", compiled) ;


   /* Read the sizes of the compiled db */

    fdcompiled = open_readb(compiled) ;
    if (!s6rc_db_read_sizes(fdcompiled, &dbblob))
      strerr_diefu3sys(111, "read ", compiled, "/n") ;


   /* Allocate enough stack for the db */

    {
      unsigned int n = dbblob.nshort + dbblob.nlong ;
      s6rc_service_t serviceblob[n] ;
      char const *argvblob[dbblob.nargvs] ;
      uint32_t depsblob[dbblob.ndeps << 1] ;
      char stringblob[dbblob.stringlen] ;
      int r ;

      dbblob.services = serviceblob ;
      dbblob.argvs = argvblob ;
      dbblob.deps = depsblob ;
      dbblob.string = stringblob ;


     /* Read the db from the file */

      r = s6rc_db_read(fdcompiled, &dbblob) ;
      if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", compiled, "/db") ;


     /* Perform the action */

      switch (what)
      {
        case 1 : /* add */
        {
          char const *const *contents = argv + 1 ;
          modify_resolve(fdcompiled, &dbblob, 0, 0, argv, &contents, 1, force, compiled) ;
          break ;
        }
        case 2 : /* delete */
          modify_resolve(fdcompiled, &dbblob, argv, argc, 0, 0, 0, force, compiled) ;
          break ;
        case 3 : /* multiple */
        {
          unsigned int toaddn, todeln ;
          char const *toadd[argc - 1] ;
          char const *const *toadd_contents[argc - 1] ;
          parse_multiple(argc, argv, &todeln, &toaddn, toadd, toadd_contents) ;
          modify_resolve(fdcompiled, &dbblob, argv, todeln, toadd, toadd_contents, toaddn, force, compiled) ;
          break ;
        }
      }
    }
  }
  return 0 ;
}
