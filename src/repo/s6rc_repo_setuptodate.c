/* ISC license. */

#include <skalibs/bsdsnowflake.h>
#include <skalibs/nonposix.h>

#include <string.h>
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

int s6rc_repo_setuptodate (char const *repo, char const *set)
{
  struct stat stsource ;
  struct stat stcompiled ;
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  char srcfn[repolen + 17 + setlen] ;
  char dstfn[repolen + 11 + setlen] ;
  memcpy(srcfn, repo, repolen) ;
  memcpy(srcfn + repolen, "/sources/", 9) ;
  memcpy(srcfn + repolen + 9, set, setlen) ;
  memcpy(srcfn + repolen + 9 + setlen, "/.stamp", 8) ;
  memcpy(dstfn, repo, repolen) ;
  memcpy(dstfn + repolen, "/compiled/", 10) ;
  memcpy(dstfn + repolen + 10, set, setlen + 1) ;
  if (stat(srcfn, &stsource) == -1)
  {
    strerr_warnfu2sys("stat ", srcfn) ;
    return -1 ;
  }
  if (stat(dstfn, &stcompiled) == -1)
  {
    if (errno == ENOENT) return 0 ;
    strerr_warnfu2sys("stat ", dstfn) ;
    return -1 ;
  }
  return
    stsource.st_mtim.tv_sec < stcompiled.st_mtim.tv_sec ? 1 :
    stsource.st_mtim.tv_sec > stcompiled.st_mtim.tv_sec ? 0 :
    stsource.st_mtim.tv_nsec < stcompiled.st_mtim.tv_nsec ;
}
