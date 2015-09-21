/* ISC license. */

#include <skalibs/uint.h>
#include <skalibs/bytestr.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-oneshot-run [ -l live ] up|down servicenumber"
#define dieusage() strerr_dieusage(100, USAGE)

int main (int argc, char const *const *argv, char const *const *envp)
{
  char const *live = S6RC_LIVE_BASE ;
  unsigned int number ;
  int up ;
  PROG = "s6-rc-db" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "l:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'l' : live = l.arg ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (argc < 2) dieusage() ;
  if (!case_diffs(argv[0], "up")) up = 1 ;
  else if (!case_diffs(argv[0], "down")) up = 0 ;
  else dieusage() ;
  if (!uint0_scan(argv[1], &number)) dieusage() ;

  {
    unsigned int livelen = str_len(live) ;
    int fdcompiled, compiledlock ;
    s6rc_db_t db ;
    char compiled[livelen + 10] ;
    byte_copy(compiled, livelen, live) ;
    byte_copy(compiled + livelen, 10, "/compiled") ;

    if (!s6rc_lock(0, 0, 0, compiled, 1, &compiledlock))
      strerr_diefu2sys(111, "take lock on ", compiled) ;
    fdcompiled = open_readb(compiled) ;
    if (fdcompiled < 0)
      strerr_diefu2sys(111, "open ", compiled) ;


   /* Read the sizes of the compiled db */

    fdcompiled = open_readb(compiled) ;
    if (!s6rc_db_read_sizes(fdcompiled, &db))
      strerr_diefu3sys(111, "read ", compiled, "/n") ;

    if (number >= db.nshort)
      strerr_dief1x(3, "invalid oneshot number") ;


   /* Allocate enough stack for the db */

    {
      s6rc_service_t serviceblob[db.nshort + db.nlong] ;
      char const *argvblob[db.nargvs] ;
      uint32 depsblob[db.ndeps << 1] ;
      char stringblob[db.stringlen] ;
      register int r ;

      db.services = serviceblob ;
      db.argvs = argvblob ;
      db.deps = depsblob ;
      db.string = stringblob ;


     /* Read the db from the file */

      r = s6rc_db_read(fdcompiled, &db) ;
      if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", compiled, "/db") ;


     /* Run the script */

      {
        register unsigned int sargc = db.services[number].x.oneshot.argc[up] ;
        char const *const *sargv = db.argvs + db.services[number].x.oneshot.argv[up] ;
        char const *newargv[sargc + 1] ;
        register char const **p = newargv ;
        while (sargc--) *p++ = *sargv++ ;
        *p = 0 ;
        pathexec_run(newargv[0], newargv, envp) ;
        strerr_dieexec(111, newargv[0]) ;
      }
    }
  }
}
