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
#include <skalibs/tai.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/direntry.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-new [ -v verbosity ] [ -r repo ] setname..."
#define dieusage() strerr_dieusage(100, USAGE)

static void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

static inline void newset (char const *repo, char const *setname)
{
  static char const sublist[4][7] = { "masked", "onboot", "bundle", "active" } ;
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(setname) ;
  char everything[repolen + 21] ;
  char fn[repolen + 10 + setlen] ;
  char tmp[repolen + 18 + setlen] ;
  char act[repolen + 25 + setlen] ;
  char onb[repolen + 25 + setlen] ;
  memcpy(everything, repo, repolen) ;
  memcpy(everything + repolen, "/sources/.everything", 21) ;
  memcpy(fn, everything, repolen + 9) ;
  memcpy(fn + repolen + 9, setname, setlen + 1) ;
  if (access(fn, F_OK) == -1)
  {
    if (errno != ENOENT) strerr_diefu2sys(111, "access ", fn) ;
  }
  else strerr_dief4x(100, "set ", setname, " already exists in repository ", repo) ;
  memcpy(tmp, fn, repolen + 9) ;
  tmp[repolen + 9] = '.' ;
  memcpy(tmp + repolen + 10, setname, setlen) ;
  memcpy(tmp + repolen + 10 + setlen, ":XXXXXX", 8) ;
  if (!mkdtemp(tmp)) strerr_diefu2sys(111, "mkdtemp ", tmp) ;

  memcpy(act, tmp, repolen + 17 + setlen) ;
  memcpy(act + repolen + 17 + setlen, "/lock", 6) ;
  if (!openwritenclose_unsafe(act, "", 0))
  {
    cleanup(tmp) ;
    strerr_diefu2sys(111, "create ", act) ;
  }

  for (size_t i = 0 ; i < 4 ; i++)
  {
    memcpy(act + repolen + 18 + setlen, sublist[i], 7) ;
    if (mkdir(act, 02755) == -1)
    {
      cleanup(tmp) ;
      strerr_diefu2sys(111, "mkdir ", act) ;
    }
  }

  DIR *dir = opendir(everything) ;
  for (;;)
  {
    char *cur = act ;
    size_t len ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;

    len = strlen(d->d_name) ;
    char src[len + 33] ;
    char dst[repolen + 26 + setlen + len] ;
    memcpy(src, "../.everything/", 15) ;
    memcpy(src + 15, d->d_name, len) ;
    memcpy(src + 15 + len, "/flag-essential", 16) ;
    if (access(src, F_OK) == -1)
    {
      if (errno != ENOENT) strerr_diefu2sys(111, "access ", src) ;
    }
    else cur = onb ;
    memcpy(src + 21 + len, "recommended", 12) ;
    if (access(src, F_OK) == -1)
    {
      if (errno != ENOENT) strerr_diefu2sys(111, "access ", src) ;
    }
    else cur = onb ;
    src[15 + len] = 0 ;
    memcpy(dst, cur, repolen + 24 + setlen) ;
    dst[repolen + 24 + setlen] = '/' ;
    memcpy(dst + repolen + 25 + setlen, d->d_name, len+1) ;
    if (symlink(src, dst) == -1)
    {
      cleanup(tmp) ;
      strerr_diefu4sys(111, "symlink ", src, " to ", dst) ;
    }
  }
  dir_close(dir) ;
  if (errno)
  {
    cleanup(tmp) ;
    strerr_diefu2sys(111, "readdir ", tmp) ;
  }

  if (chmod(tmp, 02755) == -1)
  {
    cleanup(tmp) ;
    strerr_diefu2sys(111, "chmod ", tmp) ;
  }
  if (symlink(tmp, fn) == -1)
  {
    cleanup(tmp) ;
    strerr_diefu4sys(111, "rename ", tmp, " to ", fn) ;
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

  PROG = "s6-rc-set-new" ;
  golc = gol_main(argc, argv, 0, 0, rgola, 2, 0, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (wgola[GOLA_REPODIR]) repo = wgola[GOLA_REPODIR] ;
  if (!argc) dieusage() ;
  for (unsigned int i = 0 ; i < argc ; i++)
  {
    if (!argv[i][0]) strerr_dief1x(100, "set names cannot be empty") ;
    if (argv[i][0] == '.') strerr_dief1x(100, "set names cannot start with a dot") ;
    if (strchr(argv[i], '/') || strchr(argv[i], '\n'))
      strerr_dief1x(100, "set names cannot contain / or newlines") ;
  }

  fdlock = s6rc_repo_lock(repo, 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", repo) ;
  tain_now_g() ;

  for (unsigned int i = 0 ; i < argc ; i++)
    newset(repo, argv[i]) ;

  return 0 ;
}
