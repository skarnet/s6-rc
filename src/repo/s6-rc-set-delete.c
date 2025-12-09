/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/direntry.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-delete [ -v verbosity ] [ -r repo ] setname..."
#define dieusage() strerr_dieusage(100, USAGE)

static inline void dodelete (char const *repo, char const *set)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  char fn[repolen + setlen + 11] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, set, setlen + 1) ;
  if (access(fn, W_OK) == -1)
  {
    if (errno != ENOENT) strerr_diefu2sys(111, "access ", fn) ;
  }
  else
  {
    ssize_t r ;
    char real[repolen + setlen + 18] ;
    memcpy(real, repo, repolen) ;
    memcpy(real + repolen, "/sources/", 9) ;
    r = readlink(fn, real + repolen + 9, setlen + 9) ;
    if (r == -1) strerr_diefu2sys(111, "readlink ", fn) ;
    else if (r != setlen + 8) strerr_dief3x(102, "symlink ", fn, " points to an invalid name") ;
    real[repolen + setlen + 17] = 0 ;
    unlink_void(fn) ;
    rm_rf(real) ;
  }

  memcpy(fn + repolen + 1, "compiled/", 9) ;
  memcpy(fn + repolen + 10, set, setlen + 1) ;
  unlink_void(fn) ;
  fn[repolen + 9] = 0 ;
  DIR *dir = opendir(fn) ;
  if (!dir) strerr_diefu2sys(111, "opendir ", fn) ;
  for (;;)
  {
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.' && !strncmp(d->d_name + 1, set, setlen) && d->d_name[1 + setlen] == ':' && strlen(d->d_name + 2 + setlen) == 6)
    {
      size_t len = strlen(d->d_name) ;
      char tmp[repolen + 11 + len] ;
      memcpy(tmp, fn, repolen + 9) ;
      tmp[repolen + 9] = '/' ;
      memcpy(tmp + repolen + 10, d->d_name, len + 1) ;
      rm_rf(tmp) ;
    }
  }
  if (errno) strerr_diefu2sys(111, "readdir ", fn) ;
  dir_close(dir) ;
}

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_N
} ;

static gol_arg const rgola[] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR }
} ;

int main (int argc, char const *const *argv)
{
  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int golc ;

  PROG = "s6-rc-set-delete" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = gol_main(argc, argv, 0, 0, rgola, 2, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (!argc) dieusage() ;
  for (unsigned int i = 0 ; i < argc ; i++) s6rc_repo_sanitize_setname(argv[i]) ;

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;

  for (unsigned int i = 0 ; i < argc ; i++)
    dodelete(wgola[GOLA_REPODIR], argv[i]) ;
  _exit(0) ;
}
