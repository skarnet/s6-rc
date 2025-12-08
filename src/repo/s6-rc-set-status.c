/* ISC license. */

#include <skalibs/nonposix.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/uint64.h>
#include <skalibs/envexec.h>
#include <skalibs/bytestr.h>
#include <skalibs/direntry.h>
#include <skalibs/buffer.h>
#include <skalibs/tai.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

#define USAGE "s6-rc-set-status [ -v verbosity ] [ -r repo ] [ -E | -e ] set services..."
#define dieusage() strerr_dieusage(100, USAGE)
#define dieout() strerr_diefu1sys(111, "write to stdout")

enum golb_e
{
  GOLB_IGNORE_ESSENTIALS = 0x01,
  GOLB_NOSUB = 0x02,
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_N
} ;

int main (int argc, char const **argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'E', .lo = "with-essentials", .clear = GOLB_IGNORE_ESSENTIALS, .set = 0 },
    { .so = 'e', .lo = "without-essentials", .clear = 0, .set = GOLB_IGNORE_ESSENTIALS },
    { .so = 'L', .lo = "list", .clear = 0, .set = GOLB_NOSUB },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR }
  } ;
  int fdlock ;
  unsigned int verbosity = 1 ;
  uint64_t wgolb = 0 ;
  char const *setname ;
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int golc ;
  uint8_t sub ;

  PROG = "s6-rc-set-status" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (argc < 2) dieusage() ;
  setname = *argv++ ; argc-- ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (strchr(setname, '/') || strchr(setname, '\n'))
      strerr_dief2x(100, "set", " names cannot contain / or newlines") ;
  for (unsigned int i = 0 ; i < argc ; i++)
  {
    if (argv[i][0] == '.')
      strerr_dief2x(100, "service", " names cannot start with a dot") ;
    if (strchr(argv[i], '/') || strchr(argv[i], '\n'))
      strerr_dief2x(100, "service", " names cannot contain / or newlines") ;
  }

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;

  {
    int e = s6rc_repo_checkset(wgola[GOLA_REPODIR], setname) ;
    if (e) _exit(e) ;
  }

  qsort(argv, argc, sizeof(char const *), &str_cmp) ;

  size_t repolen = strlen(wgola[GOLA_REPODIR]) ;
  size_t setlen = strlen(setname) ;
  char fn[repolen + setlen + 17] ;
  memcpy(fn, wgola[GOLA_REPODIR], repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, setname, setlen) ;
  fn[repolen + 9 + setlen] = '/' ;

  sub = wgolb & GOLB_IGNORE_ESSENTIALS ? 3 : 4 ;
  while (sub--)
  {
    memcpy(fn + repolen + 10 + setlen, s6rc_repo_subnames[sub], 7) ;
    DIR *dir = opendir(fn) ;
    if (!dir) strerr_diefu2sys(111, "opendir ", fn) ;
    for (;;)
    {
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;
      if (argc && !bsearch(d->d_name, argv, argc, sizeof(char const *), &str_bcmp)) continue ;
      if (buffer_puts(buffer_1, d->d_name) < 0) dieout() ;
      if (!(wgolb & GOLB_NOSUB))
        if (buffer_put(buffer_1, "/", 1) < 0
         || buffer_put(buffer_1, s6rc_repo_subnames[sub], 6) < 0) dieout() ;
      if (buffer_put(buffer_1, "\n", 1) < 0) dieout() ;
    }
    if (errno) strerr_diefu2sys(111, "readdir ", fn) ;
    dir_close(dir) ;
  }
  if (!buffer_flush(buffer_1)) dieout() ;
  _exit(0) ;
}
