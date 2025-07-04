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

#define USAGE "s6-rc-set-copy [ -v verbosity ] [ -r repo ] srcset dstset"
#define dieusage() strerr_dieusage(100, USAGE)

static void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

static inline void docopy (char const *repo, char const *srcname, char const *dstname)
{
  size_t repolen = strlen(repo) ;
  size_t srclen = strlen(srcname) ;
  size_t dstlen = strlen(dstname) ;
  char src[repolen + srclen + 10] ;
  char dst[repolen + dstlen + 10] ;
  memcpy(src, repo, repolen) ;
  memcpy(src + repolen, "/sources/", 9) ;
  memcpy(src + repolen + 9, srcname, srclen + 1) ;
  memcpy(dst, src, repolen + 9) ;
  memcpy(dst + repolen + 9, dstname, dstlen + 1) ;
  if (access(dst, F_OK) == -1)
  {
    if (errno != ENOENT) strerr_diefu2sys(111, "access ", dst) ;
  }
  else strerr_dief4x(100, "set ", dstname, " already exists in repository ", repo) ;
  if (!hiercopy(src, dst))
  {
    cleanup(dst) ;
    strerr_diefu4sys(111, "copy ", src, " to ", dst) ;
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

  PROG = "s6-rc-set-copy" ;
  golc = gol_main(argc, argv, 0, 0, rgola, 2, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (wgola[GOLA_REPODIR]) repo = wgola[GOLA_REPODIR] ;
  if (argc < 2) dieusage() ;
  for (unsigned int i = 0 ; i < 2 ; i++)
    if (strchr(argv[i], '/') || strchr(argv[i], '\n'))
      strerr_dief1x(100, "set names cannot contain / or newlines") ;

  fdlock = s6rc_repo_lock(repo, 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", repo) ;

  docopy(repo, argv[0], argv[1]) ;
  return 0 ;
}
