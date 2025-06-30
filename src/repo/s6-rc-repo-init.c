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

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-repo-init [ -r repo ]"
#define dieusage() strerr_dieusage(100, USAGE)

static void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

static gol_arg const rgola[1] =
{
  { .so = 'r', .lo = "repodir", .i = 0 }
} ;

int main (int argc, char const *const *argv)
{
  size_t repolen ;
  char const *repo = S6RC_REPO_BASE ;
  unsigned int golc ;

  PROG = "s6-rc-repo-init" ;
  golc = gol_main(argc, argv, 0, 0, rgola, 1, 0, &repo) ;
  argc -= golc ; argv += golc ;
  if (repo[0] != '/')
    strerr_dief2x(100, repo, " is not an absolute path") ;

  repolen = strlen(repo) ;
  char repotmp[repolen + 12] ;
  char tmp[repolen + 39] ;
  memcpy(repotmp, repo, repolen) ;
  memcpy(repotmp + repolen, "-tmp.XXXXXX", 12) ;
  umask(0) ;
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
    memcpy(repotmp + repolen, "-tmp.XXXXXX", 12) ;
    if (!mkdtemp(repotmp))
      strerr_diefu2sys(111, "mkdtemp ", repotmp) ;
  }
  memcpy(tmp, repotmp, repolen + 11) ;
  tmp[repolen + 11] = '/' ;

  memcpy(tmp + repolen + 12, "compiled", 9) ;
  if (mkdir(tmp, 02755) == -1)
    strerr_diefu2sys(111, "mkdir ", tmp) ;

  memcpy(tmp + repolen + 12, "source", 7) ;
  if (mkdir(tmp, 02755) == -1)
    strerr_diefu2sys(111, "mkdir ", tmp) ;

  memcpy(tmp + repolen + 18, "/.everything-initial", 21) ;
  if (mkdir(tmp, 02755) == -1)
    strerr_diefu2sys(111, "mkdir ", tmp) ;

  tmp[repolen + 30] = 0 ;
  if (symlink(".everything-initial", tmp) == -1)
    strerr_diefu4sys(111, "symlink ", ".everything-initial", " to ", tmp) ;

  if (chmod(repotmp, 02755) == -1)
    strerr_diefu2sys(111, "chmod ", repotmp) ;
  if (rename(repotmp, repo) == -1)
  {
    cleanup(repotmp) ;
    strerr_diefu4sys(111, "rename ", repotmp, " to ", repo) ;
  }

  return 0 ;
}
