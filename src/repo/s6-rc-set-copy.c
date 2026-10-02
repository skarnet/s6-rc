/* ISC license. */

#include <string.h>
#include <unistd.h>

#include <skalibs/uint64.h>
#include <skalibs/envexec.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-copy [ -v verbosity ] [ -r repo ] [ -f ] srcset dstset"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_FORCE = 0x01
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_N
} ;

static gol_bool const rgolb[] =
{
  { .so = 'f', .lo = "force", .clear = 0, .set = GOLB_FORCE }
} ;

static gol_arg const rgola[] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR }
} ;

static inline void docopy (char const *repo, char const *srcname, char const *dstname, uint64_t flags)
{
  int e = s6rc_repo_setcopy(repo, srcname, dstname, !!(flags & GOLB_FORCE)) ;
  switch (e)
  {
    case -2 : strerr_dief(102, "internal layout error: symlink inside ", srcname, " doesn't point to a valid name") ;
    case -1 : strerr_dief(1, "set ", dstname, " already exists in repository ", repo) ;
    case 0 : break ;
    default : strerr_diefusys(111, "copy set ", srcname, " to set ", dstname) ;
  }
}

int main (int argc, char const *const *argv)
{
  int fdlock ;
  unsigned int verbosity = 1 ;
  uint64_t wgolb = 0 ;
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int golc ;

  PROG = "s6-rc-set-copy" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (argc < 2) dieusage() ;
  if (!strcmp(argv[0], argv[1]))
    strerr_dief1x(100, "source and destination sets are the same") ;
  s6rc_repo_sanitize_setname(argv[0]) ;
  s6rc_repo_sanitize_setname(argv[1]) ;

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;
  docopy(wgola[GOLA_REPODIR], argv[0], argv[1], wgolb) ;
  _exit(0) ;
}
