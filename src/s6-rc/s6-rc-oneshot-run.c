/* ISC license. */

#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>

#include <skalibs/uint64.h>
#include <skalibs/types.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>
#include <skalibs/envexec.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-oneshot-run [ -l live ] [ -b ] up|down servicenumber"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_UP = 0x01,
  GOLB_BLOCK = 0x02,
} ;

enum gola_e
{
  GOLA_LIVEDIR,
  GOLA_N
} ;

int main (int argc, char const *const *argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 0, .lo = "no-block", .clear = GOLB_BLOCK, .set = 0 },
    { .so = 'b', .lo = "block", .clear = 0, .set = GOLB_BLOCK },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'l', .lo = "livedir", .i = GOLA_LIVEDIR },
  } ;
  uint64_t wgolb = 0 ;
  char const *wgola[GOLA_N] = { [GOLA_LIVEDIR] = S6RC_LIVEDIR } ;
  unsigned int number ;
  PROG = "s6-rc-oneshot-run" ;

  number = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= number ; argv += number ;
  if (argc < 2) dieusage() ;

  if (!strcasecmp(argv[0], "up")) wgolb |= GOLB_UP ;
  else if (!strcasecmp(argv[0], "down")) wgolb &= ~GOLB_UP ;
  else dieusage() ;
  if (!uint0_scan(argv[1], &number)) dieusage() ;

  {
    size_t livelen = strlen(wgola[GOLA_LIVEDIR]) ;
    int fdcompiled, compiledlock ;
    s6rc_db_t db ;
    char compiled[livelen + 10] ;
    memcpy(compiled, wgola[GOLA_LIVEDIR], livelen) ;
    memcpy(compiled + livelen, "/compiled", 10) ;

    if (!s6rc_lock(0, 0, 0, compiled, 1, &compiledlock, !!(wgolb & GOLB_BLOCK)))
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
        s6rc_service_t const *sv = db.services + number ;
        unsigned int namelen = strlen(db.string + sv->name) ;
        unsigned int sargc = sv->x.oneshot.argc[!!(wgolb & GOLB_UP)] ;
        char const *const *sargv = db.argvs + sv->x.oneshot.argv[!!(wgolb & GOLB_UP)] ;
        char const *newargv[sargc + 1] ;
        char const **p = newargv ;
        char modif[namelen + 9] ;
        memcpy(modif, "RC_NAME=", 8) ;
        memcpy(modif + 8, db.string + sv->name, namelen + 1) ;
        while (sargc--) *p++ = *sargv++ ;
        *p = 0 ;
        xmexec0_n(newargv, modif, namelen + 9, 1) ;
      }
    }
  }
}
