/* ISC license. */

#include <skalibs/bsdsnowflake.h>  /* stat */
#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>  /* rename */
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/posixplz.h>
#include <skalibs/gol.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>
#include <skalibs/random.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-repo-init [ -r repo ] sources..."
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
  size_t repolen ;
  char const *repo = S6RC_REPO_BASE ;
  unsigned int verbosity = 1 ;
  mode_t m ;
  char const *wgola[2] = { 0 } ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;

  PROG = "s6-rc-repo-init" ;
  golc = gol_main(argc, argv, rgolb, 1, rgola, 2, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (wgola[GOLA_REPODIR]) repo = wgola[GOLA_REPODIR] ;
  if (repo[0] != '/')
    strerr_dief2x(100, repo, " is not an absolute path") ;
  for (unsigned int i = 0 ; i < argc ; i++)
    if (argv[i][0] != '/')
      strerr_dief2x(100, argv[i], " is not an absolute path") ;
  if (!argc) strerr_warnw1x("no source directories given, creating an empty repository") ;

  repolen = strlen(repo) ;
  char repotmp[repolen + 12] ;
  char tmp[repolen + 21] ;
  memcpy(repotmp, repo, repolen) ;
  memcpy(repotmp + repolen, "-tmp.XXXXXX", 12) ;
  m = umask(0) ;
  if (!mkdtemp(repotmp))
  {
    size_t i = repolen ;
    if (errno != ENOENT)
      strerr_diefu2sys(111, "mkdtemp ", repotmp) ;
    while (i && repotmp[i] != '/') i-- ;
    repotmp[i] = 0 ;
    if (mkdirp2(repotmp, 02755) == -1)
      strerr_diefu2sys(111, "mkdir ", repotmp) ;
    repotmp[i] = '/' ;
    memcpy(repotmp + repolen, ":new:XXXXXX", 12) ;
    if (!mkdtemp(repotmp))
      strerr_diefu2sys(111, "mkdtemp ", repotmp) ;
  }
  memcpy(tmp, repotmp, repolen + 11) ;
  tmp[repolen + 11] = '/' ;

  memcpy(tmp + repolen + 12, "compiled", 9) ;
  if (mkdir(tmp, 02755) == -1)
  {
    cleanup(repotmp) ;
    strerr_diefu2sys(111, "mkdir ", tmp) ;
  }

  memcpy(tmp + repolen + 12, "source", 7) ;
  if (mkdir(tmp, 02755) == -1)
  {
    cleanup(repotmp) ;
    strerr_diefu2sys(111, "mkdir ", tmp) ;
  }

  umask(m) ;

  if (!s6rc_repo_sync(repotmp, argv, argc, verbosity))
  {
    cleanup(repotmp) ;
    strerr_diefu2sys(111, "sync ", repotmp) ;
  }

  if (chmod(repotmp, 02755) == -1)
    strerr_diefu2sys(111, "chmod ", repotmp) ;
  if (rename(repotmp, repo) == -1)
  {
    if (errno != EEXIST || !(wgolb & 1 << GOLB_FORCE))
    {
      cleanup(repotmp) ;
      strerr_diefu4sys(111, "rename ", repotmp, " to ", repo) ;
    }
    else
    {
      memcpy(tmp, repo, repolen) ;
      memcpy(tmp + repolen, ":old:", 5) ;
      random_name(tmp + repolen + 5, 15) ;
      tmp[repolen + 20] = 0 ;
      if (rename(repo, tmp) == -1)
      {
        cleanup(repotmp) ;
        strerr_diefu4sys(111, "rename ", repo, " to ", tmp) ;
      }
      if (rename(repotmp, repo) == -1)
      {
        int e = errno ;
        if (rename(tmp, repo) == -1)
          strerr_diefu7sys(111, "rename directories to ", repo, ": weird race happened. New repository is at ", repotmp, " and old repository is at ", tmp, " - reported error was") ;
        errno = e ;
        cleanup(repotmp) ;
        strerr_diefu4sys(111, "rename ", repotmp, " to ", repo) ;
      }
    }
  }

  return 0 ;
}
