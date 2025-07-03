/* ISC license. */

#include <skalibs/bsdsnowflake.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

static inline int sub_cleanup (char const *subdir)
{
  size_t dirlen = strlen(subdir) ;
  DIR *dir = opendir(subdir) ;
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
      char fn[dirlen + len + 2] ;
      memcpy(fn, dir, dirlen) ;
      fn[dirlen] = '/' ;
      memcpy(fn + dirlen + 1, d->d_name, len + 1) ;
      if (access(fn, F_OK) == -1)
      {
        if (errno == ENOENT) unlink_void(fn) ;
        else break ;
      }
      else
      {
        struct stat st ;
        if (lstat(fn, &st) == -1) break ;
        if (!S_ISLNK(st.st_mode)) rm_rf(fn) ;
      }
    }
  }
  dir_close(dir) ;
  return !errno ;
}

int s6rc_repo_cleanup (char const *repo)
{
  DIR *dir ;
  size_t repolen = strlen(repo) ;
  char sources[repolen + 9] ;
  memcpy(sources, repo, repolen) ;
  memcpy(sources + repolen, "/sources", 9) ;
  dir = opendir(sources) ;
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
      memcpy(fn, sources, repolen + 8) ;
      fn[repolen + 8] = '/' ;
      memcpy(fn + repolen + 9, d->d_name, len + 1) ;
      if (!sub_cleanup(fn)) break ;
    }
  }
  dir_close(dir) ;
  return !errno ;
}
