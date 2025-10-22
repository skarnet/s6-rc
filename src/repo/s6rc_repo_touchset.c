/* ISC license. */

#include <string.h>

#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

int s6rc_repo_touchset (char const *repo, char const *set)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  char fn[repolen + 17 + setlen] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, set, setlen) ;
  memcpy(fn + repolen + 9 + setlen, "/.stamp", 8) ;
  if (!touch(fn))
  {
    strerr_warnfu2sys("touch ", fn) ;
    return 0 ;
  }
  return 1 ;
}
