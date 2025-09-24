/* ISC license. */

#include <skalibs/bsdsnowflake.h>  /* stat */
#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>  /* rename */
#include <errno.h>

#include <skalibs/uint32.h>
#include <skalibs/stat.h>
#include <skalibs/posixplz.h>
#include <skalibs/gol.h>
#include <skalibs/tai.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>
#include <skalibs/random.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-repo-init [ -v verbosity ] [ -r repo ] [ -h fdhuser ] [ -B ] [ -f ] stores..."
#define dieusage() strerr_dieusage(100, USAGE)

static void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

enum golb_e
{
  GOLB_FORCE = 0x01,
  GOLB_BARE = 0x02
} ;

static gol_bool const rgolb[] =
{
  { .so = 'f', .lo = "force", .clear = 0, .set = GOLB_FORCE },
  { .so = 'S', .lo = "bare", .clear = 0, .set = GOLB_BARE }
} ;

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
  size_t repolen ;
  unsigned int verbosity = 1 ;
  mode_t m ;
  char const *wgola[GOLA_N] = { 0 } ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;

  PROG = "s6-rc-repo-init" ;
  wgola[GOLA_REPODIR] = S6RC_REPO_BASE ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (!argc) dieusage() ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  for (unsigned int i = 0 ; i < argc ; i++)
  {
    if (argv[i][0] != '/')
      strerr_dief2x(100, argv[i], " is not an absolute path") ;
    if (strchr(argv[i], '\n'))
      strerr_dief1x(100, "stores cannot contain newlines in their path") ;
  }
  tain_now_g() ;
  repolen = strlen(wgola[GOLA_REPODIR]) ;
  char repotmp[repolen + 12] ;
  char tmp[repolen + 29] ;
  memcpy(repotmp, wgola[GOLA_REPODIR], repolen) ;
  memcpy(repotmp + repolen, ":new:XXXXXX", 12) ;
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

  memcpy(tmp + repolen + 12, "sources", 8) ;
  if (mkdir(tmp, 02755) == -1)
  {
    cleanup(repotmp) ;
    strerr_diefu2sys(111, "mkdir ", tmp) ;
  }

  memcpy(tmp + repolen + 12, "stores", 7) ;
  if (mkdir(tmp, 02755) == -1)
  {
    cleanup(repotmp) ;
    strerr_diefu2sys(111, "mkdir ", tmp) ;
  }
  umask(m) ;
  tmp[repolen + 19] = '/' ;
  for (unsigned int i = 0 ; i < argc ; i++)
  {
    uint320_xfmt(tmp + repolen + 20, i, 8) ;
    tmp[repolen + 28] = 0 ;
    if (symlink(argv[i], tmp) == -1)
    {
      cleanup(repotmp) ;
      strerr_diefu4sys(111, "make a symlink from ", tmp, " to ", argv[i]) ;
    }
  }

  memcpy(tmp + repolen + 12, "lock", 5) ;
  if (!openwritenclose_unsafe5(tmp, "", 0, 0, 0))
  {
    cleanup(repotmp) ;
    strerr_diefu2sys(111, "create ", tmp) ;
  }

  if (!(wgolb & GOLB_BARE) && !s6rc_repo_sync(repotmp, verbosity, wgola[GOLA_FDHUSER]))
  {
    cleanup(repotmp) ;
    _exit(111) ;
  }

  if (chmod(repotmp, 02755) == -1)
    strerr_diefu2sys(111, "chmod ", repotmp) ;
  if (rename(repotmp, wgola[GOLA_REPODIR]) == -1)
  {
    if (errno != EEXIST || !(wgolb & GOLB_FORCE))
    {
      cleanup(repotmp) ;
      strerr_diefu4sys(111, "rename ", repotmp, " to ", wgola[GOLA_REPODIR]) ;
    }
    else
    {
      memcpy(tmp, wgola[GOLA_REPODIR], repolen) ;
      memcpy(tmp + repolen, ":old:", 5) ;
      random_name(tmp + repolen + 5, 15) ;
      tmp[repolen + 20] = 0 ;
      if (rename(wgola[GOLA_REPODIR], tmp) == -1)
      {
        cleanup(repotmp) ;
        strerr_diefu4sys(111, "rename ", wgola[GOLA_REPODIR], " to ", tmp) ;
      }
      if (rename(repotmp, wgola[GOLA_REPODIR]) == -1)
      {
        int e = errno ;
        if (rename(tmp, wgola[GOLA_REPODIR]) == -1)
          strerr_diefu7sys(111, "rename directories to ", wgola[GOLA_REPODIR], ": weird race happened. New repository is at ", repotmp, " and old repository is at ", tmp, " - reported error was") ;
        errno = e ;
        cleanup(repotmp) ;
        strerr_diefu4sys(111, "rename ", repotmp, " to ", wgola[GOLA_REPODIR]) ;
      }
    }
  }

  _exit(0) ;
}
