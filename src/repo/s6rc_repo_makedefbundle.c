/* ISC license. */

#include <skalibs/bsdsnowflake.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/direntry.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/skamisc.h>

#include <s6-rc/repo.h>

int s6rc_repo_makedefbundle (char const *repo, char const *set, char const *bundle)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t bundlelen = strlen(bundle) ;
  size_t bfnlen = repolen + setlen + bundlelen + 28 ;
  mode_t m ;
  DIR *dir ;
  char bfn[bfnlen + 1] ;
  char subfn[repolen + setlen + 17] ;

  memcpy(bfn, repo, repolen) ;
  memcpy(bfn + repolen, "/sources/.atomics/", 18) ;
  memcpy(bfn + repolen + 18, bundle, bundlelen + 1) ;
  
  if (access(bfn, F_OK) == 0) goto errexists ;
  if (errno != ENOENT)
  {
    strerr_warnfu2sys("check existence of ", bfn) ;
    return 0 ;
  }
  memcpy(bfn + repolen + 10, "bundle", 6) ;
  if (access(bfn, F_OK) == 0) goto errexists ;
  if (errno != ENOENT)
  {
    strerr_warnfu2sys("check existence of ", bfn) ;
    return 0 ;
  }

  memcpy(bfn + repolen + 9, set, setlen) ;
  memcpy(bfn + repolen + 9 + setlen, "/bundle/", 8) ;
  memcpy(bfn + repolen + 17 + setlen, bundle, bundlelen + 1) ;
  m = umask(0) ;
  if (mkdir(bfn, 02755) == -1)
  {
    if (errno != EEXIST) strerr_warnfu2sys("mkdir ", bfn) ;
    else strerr_warnf6x("bundle ", bundle, " is already defined in set ", set, " of repository ", repo) ;
    umask(m) ;
    return 0 ;
  }
  memcpy(bfn + repolen + setlen + 17 + bundlelen, "/contents.d", 12) ;
  if (mkdir(bfn, 02755) == -1)
  {
    strerr_warnfu2sys("mkdir ", bfn) ;
    umask(m) ;
    return 0 ;
  }
  umask(m) ;
  memcpy(bfn + repolen + setlen + 18 + bundlelen, "type", 5) ;
  if (!openwritenclose_unsafe(bfn, "bundle\n", 7))
  {
    strerr_warnfu2sys("write to ", bfn) ;
    return 0 ;
  }
  memcpy(bfn + repolen + setlen + 18 + bundlelen, "contents.d/", 11) ;

  memcpy(subfn, bfn, repolen + 10 + setlen) ;
  for (uint8_t sub = 2 ; sub < 4 ; sub++)
  {
    memcpy(subfn + repolen + 10 + setlen, s6rc_repo_subnames[sub], 7) ;
    dir = opendir(subfn) ;
    if (!dir)
    {
      strerr_warnfu2sys("opendir ", subfn) ;
      return 0 ;
    }

    for (;;)
    {
      size_t len ;
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;
      len = strlen(d->d_name) ;
      char fn[bfnlen + len + 2] ;
      memcpy(fn, bfn, bfnlen + 1) ;
      memcpy(fn + bfnlen + 1, d->d_name, len + 1) ;
      if (!openwritenclose_unsafe(fn, "", 0))
      {
        strerr_warnfu2sys("write to ", fn) ;
        dir_close(dir) ;
        return 0 ;
      }
    }
    dir_close(dir) ;
    if (errno)
    {
      strerr_warnfu2sys("readdir ", subfn) ;
      return 0 ;
    }
  }

  return 1 ;

 errexists:
  if (sareadlink(&satmp, bfn) == -1)
  {
    strerr_warnfu2sys("readlink ", bfn) ;
    return 0 ;
  }
  if (!stralloc_0(&satmp))
  {
    strerr_warnfu1sys("stralloc_catb ") ;
    satmp.len = 0 ;
    return 0 ;
  }
  strerr_warnf4x("bundle ", bundle, " is already defined at ", satmp.s) ;
  satmp.len = 0 ;
  return 0 ;
}
