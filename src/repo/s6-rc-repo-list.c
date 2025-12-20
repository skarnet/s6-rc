/* ISC license. */

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <skalibs/buffer.h>
#include <skalibs/direntry.h>
#include <skalibs/envexec.h>
#include <skalibs/tai.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

#define USAGE "s6-rc-repo-list [ -v verbosity ] [ -r repo ] [ -x exclude ]"
#define dieusage() strerr_dieusage(100, USAGE)
#define dieout() strerr_diefu1sys(111, "write to stdout")

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_EXCLUDE,
  GOLA_N
} ;

static gol_arg const rgola[] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR },
  { .so = 'x', .lo = "exclude", .i = GOLA_EXCLUDE }
} ;

int main (int argc, char const *const *argv)
{
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int verbosity = 1 ;
  unsigned int golc ;
  int fdlock ;

  PROG = "s6-rc-repo-list" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = gol_main(argc, argv, 0, 0, rgola, 3, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;

  size_t repolen = strlen(wgola[GOLA_REPODIR]) ;
  char fn[repolen + 9] ;
  memcpy(fn, wgola[GOLA_REPODIR], repolen) ;
  memcpy(fn + repolen, "/sources", 9) ;
  DIR *dir = opendir(fn) ;
  if (!dir) strerr_diefu2sys(111, "opendir ", fn) ;
  for (;;)
  {
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    if (wgola[GOLA_EXCLUDE] && !strcmp(d->d_name, wgola[GOLA_EXCLUDE])) continue ;
    if (buffer_puts(buffer_1, d->d_name) < 0
     || buffer_put(buffer_1, "\n", 1) < 0) dieout() ;
  }
  if (errno) strerr_diefu2sys(111, "readdir ", fn) ;
  // dir_close(dir) ;
  if (!buffer_flush(buffer_1)) dieout() ;
  _exit(0) ;
}
