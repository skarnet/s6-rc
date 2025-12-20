/* ISC license. */

#include <skalibs/bsdsnowflake.h>  /* stat */
#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>  /* rename */
#include <errno.h>

#include <skalibs/uint32.h>
#include <skalibs/stat.h>
#include <skalibs/posixplz.h>
#include <skalibs/gol.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/random.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

#define USAGE "s6-rc-repo-init [ -v verbosity ] [ -r repo ] [ -h fdhuser ] [ -f ] [ -U ] [ -B ] stores..."
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
  GOLB_UPDATE = 0x02,
  GOLB_BARE = 0x04
} ;

static gol_bool const rgolb[] =
{
  { .so = 'f', .lo = "force", .clear = 0, .set = GOLB_FORCE },
  { .so = 'U', .lo = "update-stores", .clear = 0, .set = GOLB_UPDATE },
  { .so = 'B', .lo = "bare", .clear = 0, .set = GOLB_BARE }
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
  { .so = 'h', .lo = "fdholder-user", .i = GOLA_FDHUSER }
} ;

int main (int argc, char const *const *argv)
{
  unsigned int verbosity = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;
  size_t repolen ;
  int lockfd ;

  PROG = "s6-rc-repo-init" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (!argc) dieusage() ;
  if (argc > UINT16_MAX) strerr_dief1x(100, "too many stores specified") ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  for (uint16_t i = 0 ; i < argc ; i++)
  {
    if (argv[i][0] != '/')
      strerr_dief2x(100, argv[i], " is not an absolute path") ;
    if (strchr(argv[i], '\n'))
      strerr_dief1x(100, "stores cannot contain newlines in their path") ;
  }
  repolen = strlen(wgola[GOLA_REPODIR]) ;
  tain_now_g() ;

  if (!(wgolb & GOLB_UPDATE))
  {
    mode_t m ;
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

    memcpy(tmp + repolen + 12, "lock", 5) ;
    if (!openwritenclose_unsafe5(tmp, "", 0, 0, 0))
    {
      cleanup(repotmp) ;
      strerr_diefu2sys(111, "create ", tmp) ;
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
    umask(m) ;
  }

  lockfd = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (lockfd == -1)
    strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;

  char sold[repolen + 16] ;
  memcpy(sold, wgola[GOLA_REPODIR], repolen) ;
  sold[repolen] = '/' ;

  if (!s6rc_repo_makestores(wgola[GOLA_REPODIR], argv, argc, sold + repolen + 1)) _exit(111) ;

  if (!(wgolb & GOLB_BARE))
  {
    int r = s6rc_repo_sync(wgola[GOLA_REPODIR], verbosity, wgola[GOLA_FDHUSER]) ;
    if (r <= 0)
    {
      if (sold[repolen+1])
      {
        char stores[repolen + 8] ;
        char snew[repolen + 16] ;
        memcpy(stores, wgola[GOLA_REPODIR], repolen) ;
        memcpy(stores + repolen, "/stores", 8) ;
        memcpy(snew, wgola[GOLA_REPODIR], repolen) ;
        snew[repolen] = '/' ;
        if (!atomic_symlink4(sold + repolen + 1, stores, snew + repolen + 1, 15))
          strerr_diefu7sys(111, "atomically switch back stores at ", wgola[GOLA_REPODIR], " - old store is ", sold + repolen + 1, " and new (invalid) store is ", snew + repolen + 1, " - reported error was") ;
        rm_rf(snew) ;
      }
      _exit(r ? 111 : 1) ;
    }
  }

  if (sold[repolen+1]) rm_rf(sold) ;
  _exit(0) ;
}
