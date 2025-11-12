/* ISC license. */

#include <skalibs/nonposix.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/envexec.h>
#include <skalibs/direntry.h>
#include <skalibs/buffer.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

#define USAGE "s6-rc-set-list [ -v verbosity ] [ -r repo ] set"
#define dieusage() strerr_dieusage(100, USAGE)

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_N
} ;

int main (int argc, char const *const *argv)
{
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR }
  } ;
  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int golc ;
  int e ;
  uint8_t sub = 4 ;

  PROG = "s6-rc-set-list" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = gol_main(argc, argv, 0, 0, rgola, GOLA_N, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (!argc) dieusage() ;
  if (strchr(argv[0], '/') || strchr(argv[0], '\n'))
      strerr_dief1x(100, "set names cannot contain / or newlines") ;

  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;

  e = s6rc_repo_checkset(wgola[GOLA_REPODIR], argv[0]) ;
  if (e) _exit(e) ;

  size_t repolen = strlen(wgola[GOLA_REPODIR]) ;
  size_t setlen = strlen(argv[0]) ;
  char fn[repolen + setlen + 17] ;
  memcpy(fn, wgola[GOLA_REPODIR], repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, argv[0], setlen) ;
  fn[repolen + 9 + setlen] = '/' ;
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
      if (buffer_puts(buffer_1, d->d_name) < 0
       || buffer_put(buffer_1, "/", 1) < 0
       || buffer_put(buffer_1, s6rc_repo_subnames[sub], 6) < 0
       || buffer_put(buffer_1, "\n", 1) < 0)
        strerr_diefu1sys(111, "write to stdout") ;
    }
    if (errno) strerr_diefu2sys(111, "readdir ", fn) ;
    dir_close(dir) ;
  }
  if (!buffer_flush(buffer_1)) strerr_diefu1sys(111, "write to stdout") ;
  _exit(0) ;
}
