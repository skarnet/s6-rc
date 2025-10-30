/* ISC license. */

#include <unistd.h>

#include <skalibs/gol.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-repo-sync [ -v verbosity ] [ -r repo ] [ -h fdhuser ]"
#define dieusage() strerr_dieusage(100, USAGE)

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_FDHUSER,
  GOLA_N
} ;

static gol_arg const rgola[] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR },
  { .so = 'h', .lo = "fdholder-user", .i = GOLA_FDHUSER }
} ;

int main (int argc, char const *const *argv)
{
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int verbosity = 1 ;
  unsigned int golc ;
  int fdlock, r ;

  PROG = "s6-rc-repo-sync" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = gol_main(argc, argv, 0, 0, rgola, 3, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;

  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;

  r = s6rc_repo_sync(wgola[GOLA_REPODIR], verbosity, wgola[GOLA_FDHUSER]) ;
  if (r <= 0) _exit(r ? 111 : 1) ;
  if (!s6rc_repo_touch(wgola[GOLA_REPODIR])) _exit(111) ;

  _exit(0) ;
}
