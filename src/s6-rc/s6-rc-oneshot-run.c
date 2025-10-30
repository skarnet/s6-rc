/* ISC license. */

#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>

#include <skalibs/types.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>
#include <skalibs/exec.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-oneshot-run [ -l live ] [ -b ] up|down servicenumber"
#define dieusage() strerr_dieusage(100, USAGE)

int main (int argc, char const *const *argv)
{
  char const *live = S6RC_LIVEDIR ;
  unsigned int number ;
  int up ;
  int blocking = 0 ;
  PROG = "s6-rc-oneshot-run" ;
  {
    subgetopt l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "l:b", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'l' : live = l.arg ; break ;
        case 'b' : blocking = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (argc < 2) dieusage() ;
  if (!strcasecmp(argv[0], "up")) up = 1 ;
  else if (!strcasecmp(argv[0], "down")) up = 0 ;
  else dieusage() ;
  if (!uint0_scan(argv[1], &number)) dieusage() ;

  {
    size_t livelen = strlen(live) ;
    int fdcompiled, compiledlock ;
    s6rc_db_t db ;
    char compiled[livelen + 10] ;
    memcpy(compiled, live, livelen) ;
    memcpy(compiled + livelen, "/compiled", 10) ;

    if (!s6rc_lock(0, 0, 0, compiled, 1, &compiledlock, blocking))
      strerr_diefu2sys(111, "take lock on ", compiled) ;
    fdcompiled = open_readb(compiled) ;
    if (fdcompiled < 0)
      strerr_diefu2sys(111, "open ", compiled) ;


   /* Read the sizes of the compiled db */

    if (!s6rc_db_read_sizes(fdcompiled, &db))
      strerr_diefu3sys(111, "read ", compiled, "/n") ;

    if (number < db.nlong || number >= db.nlong + db.nshort)
      strerr_dief1x(3, "invalid service number") ;


   /* Allocate enough stack for the db */

    {
      s6rc_service_t serviceblob[db.nshort + db.nlong] ;
      char const *argvblob[db.nargvs] ;
      uint32_t depsblob[db.ndeps << 1] ;
      uint32_t producersblob[db.nproducers] ;
      char stringblob[db.stringlen] ;
      int r ;

      db.services = serviceblob ;
      db.argvs = argvblob ;
      db.deps = depsblob ;
      db.producers = producersblob ;
      db.string = stringblob ;


     /* Read the db from the file */

      r = s6rc_db_read(fdcompiled, &db) ;
      if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", compiled, "/db") ;
      close(fdcompiled) ;
      close(compiledlock) ;

     /* Run the script */

      {
        unsigned int sargc = db.services[number].x.oneshot.argc[up] ;
        char const *const *sargv = db.argvs + db.services[number].x.oneshot.argv[up] ;
        char const *newargv[sargc + 1] ;
        char const **p = newargv ;
        while (sargc--) *p++ = *sargv++ ;
        *p = 0 ;
        xexec0(newargv) ;
      }
    }
  }
}
