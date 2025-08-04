/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-delete [ -v verbosity ] [ -r repo ] setname..."
#define dieusage() strerr_dieusage(100, USAGE)

static inline void dodelete (char const *repo, char const *setname)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(setname) ;
  char fn[repolen + setlen + 10] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, setname, setlen + 1) ;
  if (access(fn, W_OK) == -1)
  {
    if (errno != ENOENT) strerr_diefu2sys(111, "access ", fn) ;
    else strerr_warnwu2sys("delete set ", setname) ;
  }
  else
  {
    ssize_t r ;
    char real[repolen + setlen + 18] ;
    memcpy(real, repo, repolen) ;
    memcpy(real + repolen, "/sources/", 9) ;
    r = readlink(fn, real + repolen + 9, setlen + 9) ;
    if (r == -1) strerr_diefu2sys(111, "readlink ", fn) ;
    else if (r != 8) strerr_dief3x(102, "symlink ", fn, " points to an invalid name") ;
    real[repolen + setlen + 17] = 0 ;
    if (unlink(fn) == -1) strerr_diefu2sys(111, "unlink ", fn) ;
    rm_rf(real) ;
  }
}

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_N
} ;

static gol_arg const rgola[2] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR }
} ;

int main (int argc, char const *const *argv)
{
  char const *repo = S6RC_REPO_BASE ;
  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[2] = { 0 } ;
  unsigned int golc ;

  PROG = "s6-rc-set-delete" ;
  golc = gol_main(argc, argv, 0, 0, rgola, 2, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (wgola[GOLA_REPODIR]) repo = wgola[GOLA_REPODIR] ;
  if (!argc) dieusage() ;
  for (unsigned int i = 0 ; i < argc ; i++)
    if (strchr(argv[i], '/') || strchr(argv[i], '\n'))
      strerr_dief1x(100, "set names cannot contain / or newlines") ;

  fdlock = s6rc_repo_lock(repo, 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", repo) ;

  for (unsigned int i = 0 ; i < argc ; i++)
    dodelete(repo, argv[i]) ;
  return 0 ;
}
