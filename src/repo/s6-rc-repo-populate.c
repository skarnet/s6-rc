/* ISC license. */

#include <skalibs/bsdsnowflake.h>  /* stat */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>  /* rename */
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/uint64.h>
#include <skalibs/posixplz.h>
#include <skalibs/gol.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-repo-populate [ -r repo ] sourcedirs..."
#define dieusage() strerr_dieusage(100, USAGE)

static void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

enum golb_e
{
  GOLB_FORCE,
  GOLB_N
} ;

static gol_bool const rgolb[1] =
{
  { .so = 'f', .lo = "force", .clear = 0, .set = 1 << GOLB_FORCE }
} ;

static gol_arg const rgola[1] =
{
  { .so = 'r', .lo = "repodir", .i = 0 }
} ;

int main (int argc, char const *const *argv)
{
  size_t repolen ;
  char const *repo = S6RC_REPO_BASE ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;

  PROG = "s6-rc-repo-populate" ;
  golc = gol_main(argc, argv, rgolb, 1, rgola, 1, &wgolb, &repo) ;
  argc -= golc ; argv += golc ;
  if (!argc) dieusage() ;
  if (repo[0] != '/')
    strerr_dief2x(100, repo, " is not an absolute path") ;

  repolen = strlen(repo) ;
  char sub[repolen + 30] ;
  memcpy(sub, repo, repolen) ;
  if (!(wgolb & 1 << GOLB_FORCE))
  {
    struct stat st ;
    
  }
  memcpy(sub + repolen, "/.everything-tmp.XXXXXX", 12) ;

  return 0 ;
}
