/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/strerr.h>

#include <s6-rc/s6rc-utils.h>
#include <s6-rc/repo.h>

int s6rc_repo_checkset (char const *repo, char const *set)
{
  struct stat st ;
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  char fn[repolen + 10 + setlen] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, set, setlen + 1) ;
  if (stat(fn, &st) == -1)
  {
    if (errno == ENOENT)
    {
      strerr_warnf4x("set ", set, " does not exist in repository ", repo) ;
      return 3 ;
    }
    else
    {
      strerr_warnfu2sys("stat ", fn) ;
      return 111 ;
    }
  }
  if (!S_ISDIR(st.st_mode))
  {
    strerr_warnf3x("file ", fn, " is not a directory") ;
    return 102 ;
  }
  return 0 ;
}
