/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/direntry.h>
#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

int s6rc_repo_touch (char const *repo)
{
  DIR *dir ;
  size_t repolen = strlen(repo) ;
  char sources[repolen + 9] ;
  memcpy(sources, repo, repolen) ;
  memcpy(sources + repolen, "/sources", 9) ;

  dir = opendir(sources) ;
  if (!dir)
  {
    strerr_warnfu2sys("opendir ", sources) ;
    return 0 ;
  }
  for (;;)
  {
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    if (!s6rc_repo_touchset(repo, d->d_name))
    {
      dir_close(dir) ;
      return 0 ;
    }
  }
  dir_close(dir) ;
  if (errno)
  {
    strerr_warnfu2sys("readdir ", sources) ;
    return 0 ;
  }
  return 1 ;
}
