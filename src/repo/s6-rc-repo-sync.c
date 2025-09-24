/* ISC license. */

#include <unistd.h>

#include <skalibs/gol.h>
#include <skalibs/tai.h>
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
  { .so = 'h', .lo = "fd-holder-user", .i = GOLA_FDHUSER }
} ;

int main (int argc, char const *const *argv)
{
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int verbosity = 1 ;
  unsigned int golc ;

  PROG = "s6-rc-repo-sync" ;
  wgola[GOLA_REPODIR] = S6RC_REPO_BASE ;

  golc = gol_main(argc, argv, 0, 0, rgola, 3, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  tain_now_g() ;

  if (!s6rc_repo_sync(wgola[GOLA_REPODIR], verbosity, wgola[GOLA_FDHUSER])) _exit(111) ;

  _exit(0) ;
}
