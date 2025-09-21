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
#include <skalibs/tai.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-commit [ -v verbosity ] [ -r repo ] [ -D defaultbundle ] [ -h fdhuser ] [ -K ] set"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_KEEPOLD = 0x01
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
  { .so = 'K', .lo = "keep-old", .clear = 0, .set = GOLB_KEEPOLD }
} ;

static gol_arg const rgola[] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR },
  { .so = 'D', .lo = "default-bundle", .i = GOLA_DEFBUNDLE },
  { .so = 'h', .lo = "fd-holder-user", .i = GOLA_FDHUSER }
} ;

static uint64_t wgolb = 0 ;

static void check_set (char const *repo, char const *set)
{
  struct stat st ;
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  char fn[repolen + 10 + setlen] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, set, setlen + 1) ;
  if (stat(fn, &st) == -1)
  {
    if (errno == ENOENT)
      strerr_dief4x(3, "set ", set, " does not exist in repository ", repo) ;
    else strerr_diefu2sys(111, "stat ", fn) ;
  }
  if (!S_ISDIR(st.st_mode))
    strerr_dief3x(102, "file ", fn, " is not a directory") ;
}

int main (int argc, char const *const *argv)
{
  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  unsigned int golc ;
  int r ;

  PROG = "s6-rc-set-commit" ;
  wgola[GOLA_REPODIR] = S6RC_REPO_BASE ;
  wgola[GOLA_DEFBUNDLE] = "default" ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (!argc) dieusage() ;
  if (strchr(argv[0], '/') || strchr(argv[0], '\n'))
    strerr_dief1x(100, "set names cannot contain / or newlines") ;

  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;
  check_set(wgola[GOLA_REPODIR], argv[0]) ;
  tain_now_g() ;

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
