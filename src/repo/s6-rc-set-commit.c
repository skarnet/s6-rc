/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/uint64.h>
#include <skalibs/posixplz.h>
#include <skalibs/stat.h>
#include <skalibs/prog.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-commit [ -v verbosity ] [ -r repo ] [ -D defaultbundle ] [ -h fdhuser ] [ -K ] [ -f ] set"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_KEEPOLD = 0x01,
  GOLB_FORCE = 0x02
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_DEFBUNDLE,
  GOLA_FDHUSER,
  GOLA_N
} ;

static gol_bool const rgolb[] =
{
  { .so = 'K', .lo = "keep-old", .clear = 0, .set = GOLB_KEEPOLD },
  { .so = 'f', .lo = "force", .clear = 0, .set = GOLB_FORCE }
} ;

static gol_arg const rgola[] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR },
  { .so = 'D', .lo = "default-bundle", .i = GOLA_DEFBUNDLE },
  { .so = 'h', .lo = "fdholder-user", .i = GOLA_FDHUSER }
} ;

int main (int argc, char const *const *argv)
{
  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;
  int r ;

  PROG = "s6-rc-set-commit" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;
  wgola[GOLA_DEFBUNDLE] = S6RC_DEFBUNDLE ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (!argc) dieusage() ;
  s6rc_repo_sanitize_setname(argv[0]) ;

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;
  r = s6rc_repo_checkset(wgola[GOLA_REPODIR], argv[0]) ;
  if (r) _exit(r) ;
  if (!(wgolb & GOLB_FORCE))
  {
    r = s6rc_repo_setuptodate(wgola[GOLA_REPODIR], argv[0]) ;
    if (r == -1) _exit(111) ;
    if (r)
    {
      if (verbosity >= 2) strerr_warni3x("set ", argv[0], " is already up-to-date.") ;
      _exit(0) ;
    }
  }

  {
    stralloc sa = STRALLOC_ZERO ;
    genalloc svlist = GENALLOC_ZERO ;  /* s6rc_repo_sv */
    genalloc badga = GENALLOC_ZERO ;  /* uint32_t */
    genalloc gatmp = GENALLOC_ZERO ;  /* size_t whatever */
    int e = s6rc_repo_fixset(wgola[GOLA_REPODIR], argv[0], 8, verbosity, &sa, &svlist, &badga, &gatmp) ;
    if (e == 1) strerr_diefu1x(1, "commit: found inconsistent dependencies") ;
    if (e) _exit(e) ;
    // genalloc_free(uint32_t, &badga) ;
    // genalloc_free(s6rc_repo_sv, &svlist) ;
    // genalloc_free(&gatmp) ;
    // stralloc_free(&sa) ;
  }

  size_t oldclen = S6RC_REPO_COMPILE_BUFLEN(strlen(wgola[GOLA_REPODIR]), strlen(argv[0])) ;
  char oldc[oldclen] ;

  r = s6rc_repo_setcompile(wgola[GOLA_REPODIR], argv[0], wgola[GOLA_DEFBUNDLE], oldc, verbosity, wgola[GOLA_FDHUSER]) ;
  if (r == -1) _exit(111) ;
  if (!r) _exit(1) ;
  if (r == 2) 
  {
    if (wgolb & GOLB_KEEPOLD)
    {
      oldc[oldclen - 1] = '\n' ;
      if (allwrite(1, oldc, oldclen) < oldclen)
        strerr_diefu1sys(111, "write to stdout") ;
      oldc[oldclen - 1] = 0 ;
    }
    else rm_rf(oldc) ;
  }
  _exit(0) ;
}
