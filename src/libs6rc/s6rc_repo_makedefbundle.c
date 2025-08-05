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

#include <s6-rc/repo.h>

int s6rc_repo_makedefbundle (char const *repo, char const *set, char const *bundle)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t bundlelen = strlen(bundle) ;
  size_t bfnlen = repolen + setlen + bundlelen + 28 ;
  DIR *dir ;
  char bfn[bfnlen + 1] ;
  char subfn[repolen + setlen + 17] ;

  memcpy(bfn, repo, repolen) ;
  memcpy(bfn + repolen, "/sources/.everything/", 21) ;
  memcpy(bfn + repolen + 21, bundle, bundlelen + 1) ;
  if (access(bfn, F_OK) == -1)
  {
    if (errno != ENOENT)
    {
      strerr_warnfu2sys("check existence of ", bfn) ;
      return 0 ;
    }
  }
  else
  {
    stralloc sa = STRALLOC_ZERO ;
    if (sareadlink(&sa, bfn) == -1)
    {
      strerr_warnfu2sys("readlink ", bfn) ;
      return 0 ;
    }
    if (!stralloc_0(&sa))
    {
      strerr_warnfu1sys("stralloc_catb ") ;
      stralloc_free(&sa) ;
      return 0 ;
    }
    strerr_warnf4x("bundle ", bundle, " is already defined at ", sa.s) ;
    stralloc_free(&sa) ;
    return (errno = EINVAL, 0) ;
  }

  memcpy(bfn + repolen + 9, set, setlen) ;
  memcpy(bfn + repolen + 9 + setlen, "/bbuild/", 8) ;
  memcpy(bfn + repolen + 17 + setlen, bundle, bundlelen + 1) ;
  if (mkdir(bfn, 02755) == -1)
  {
    if (errno != EEXIST) strerr_warnfu2sys("mkdir ", bfn) ;
    else strerr_warnf6x("bundle ", bundle, " is already defined in set ", set, " of repository ", repo) ;
    return 0 ;
  }
  memcpy(bfn + repolen + setlen + 17 + bundlelen, "/type", 6) ;
  if (!openwritenclose_unsafe(bfn, "bundle\n", 7))
  {
    strerr_warnfu2sys("write to ", bfn) ;
    return 0 ;
  }
  memcpy(bfn + repolen + setlen + 18 + bundlelen, "contents.d", 11) ;
  if (mkdir(bfn, 02755) == -1)
  {
    strerr_warnfu2sys("mkdir ", bfn) ;
    return 0 ;
  }
  bfn[bfnlen] = '/' ;

  memcpy(subfn, bfn, repolen + 10 + setlen) ;
  memcpy(subfn + repolen + 10 + setlen, "onboot", 7) ;
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

  return 1 ;
}
