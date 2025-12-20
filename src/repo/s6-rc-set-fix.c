/* ISC license. */

#include <unistd.h>

#include <skalibs/gccattributes.h>
#include <skalibs/uint64.h>
#include <skalibs/envexec.h>
#include <skalibs/tai.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

enum golb_e
{
  GOLB_FIXUP = 0x01,
  GOLB_FORCE_ESSENTIAL = 0x02,
  GOLB_DRYRUN = 0x04, 
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_N
} ;

static int fixall (char const *repo, unsigned int options, unsigned int verbosity, stralloc *sa, genalloc *svlist, stralloc *badga)
{
  stralloc setnames = STRALLOC_ZERO ;
  genalloc setindices = GENALLOC_ZERO ;  /* size_t */
  int n = s6rc_repo_list_sets(repo, &setnames, &setindices) ;
  if (n == -1) { strerr_warnfu2sys("list sets at ", repo) ; return 111 ; }
  for (unsigned int i = 0 ; i < n ; i++)
  {
    int e = s6rc_repo_fixset(repo, setnames.s + genalloc_s(size_t, &setindices)[i], options, verbosity, sa, svlist, badga) ;
    if (e) return e ;
  }
  // genalloc_free(size_t, &setindices) ;
  // stralloc_free(&setnames) ;
  return 0 ;
}

int main (int argc, char const **argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'u', .lo = "fix-up", .clear = 0, .set = GOLB_FIXUP },
    { .so = 'd', .lo = "fix-down", .clear = GOLB_FIXUP, .set = 0 },
    { .so = 'E', .lo = "no-force-essential", .clear = GOLB_FORCE_ESSENTIAL, .set = 0 },
    { .so = 'e', .lo = "force-essential", .clear = 0, .set = GOLB_FORCE_ESSENTIAL },
    { .so = 'n', .lo = "dry-run", .clear = 0, .set = GOLB_DRYRUN },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR }
  } ;
  stralloc sa = STRALLOC_ZERO ;
  genalloc svlist = GENALLOC_ZERO ;
  genalloc badga = GENALLOC_ZERO ;
  unsigned int verbosity = 1 ;
  int fdlock ;
  uint64_t wgolb = 0 ;
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int golc ;

  PROG = "s6-rc-set-fix" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  for (unsigned int i = 0 ; i < argc ; i++) s6rc_repo_sanitize_setname(argv[i]) ;

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;

  if (!argc) _exit(fixall(wgola[GOLA_REPODIR], wgolb & 7, verbosity, &sa, &svlist, &badga)) ;

  for (unsigned int i = 0 ; i < argc ; i++)
  {
    int e = s6rc_repo_checkset(wgola[GOLA_REPODIR], argv[i]) ;
    if (e) _exit(e) ;
  }
  for (unsigned int i = 0 ; i < argc ; i++)
  {
    int e = s6rc_repo_fixset(wgola[GOLA_REPODIR], argv[i], wgolb & 7, verbosity, &sa, &svlist, &badga) ;
    if (e) _exit(e) ;
  }
  _exit(0) ;
}
