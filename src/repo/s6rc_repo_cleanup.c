/* ISC license. */

#include <skalibs/bsdsnowflake.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

static inline int sub_cleanup (char const *sub)
{
  size_t sublen = strlen(sub) ;
  DIR *dir = opendir(sub) ;
  if (!dir) return 0 ;
  for (;;)
  {
    size_t len ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    len = strlen(d->d_name) ;
    {
      char fn[sublen + len + 2] ;
      memcpy(fn, sub, sublen) ;
      fn[sublen] = '/' ;
      memcpy(fn + sublen + 1, d->d_name, len + 1) ;
      if (access(fn, F_OK) == 0)
      {
        struct stat st ;
        if (lstat(fn, &st) == -1) break ;
        if (!S_ISLNK(st.st_mode)) rm_rf(fn) ;
      }
      else if (errno != ENOENT)
      {
        strerr_warnfu2sys("access ", fn) ;
        break ;
      }
      else unlink_void(fn) ;
    }
  }
  dir_close(dir) ;
  return !errno ;
}

static inline int set_cleanup (char const *set)
{
  size_t setlen = strlen(set) ;
  DIR *dir ;
  {
    char bundle[setlen + 8] ;
    memcpy(bundle, set, setlen) ;
    memcpy(bundle + setlen, "/bundle", 8) ;
    rm_rf(bundle) ;
  }
  dir = opendir(set) ;
  if (!dir) return 0 ;
  for (;;)
  {
    size_t len ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    len = strlen(d->d_name) ;
    {
      char fn[setlen + len + 2] ;
      memcpy(fn, set, setlen) ;
      fn[setlen] = '/' ;
      memcpy(fn + setlen + 1, d->d_name, len + 1) ;
      if (!sub_cleanup(fn)) break ;
    }
  }
  dir_close(dir) ;
  return !errno ;
}

int s6rc_repo_sources_cleanup (char const *repo)
{
  DIR *dir ;
  size_t repolen = strlen(repo) ;
  unsigned int nsets = 0 ;
  char sets[repolen + 9] ;
  memcpy(sets, repo, repolen) ;
  memcpy(sets + repolen, "/sources", 9) ;
  dir = opendir(sets) ;
  if (!dir) return 0 ;
  for (;;)
  {
    size_t len ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    len = strlen(d->d_name) ;
    {
      char fn[repolen + len + 10] ;
      memcpy(fn, sets, repolen + 8) ;
      fn[repolen + 8] = '/' ;
      memcpy(fn + repolen + 9, d->d_name, len + 1) ;
      if (!set_cleanup(fn)) break ;
      nsets++ ;
    }
  }
  dir_close(dir) ;
  return errno ? -1 : nsets ;
}
