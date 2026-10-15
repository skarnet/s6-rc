/* ISC license. */

#include <skalibs/bsdsnowflake.h>
#include <skalibs/nonposix.h>

#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>

#include <skalibs/stat.h>
#include <skalibs/strerr.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc-utils.h>
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
    strerr_warnfusys("stat ", srcfn) ;
    return -1 ;
  }

 /* Do we have a compiled db for this set? */
  if (stat(dstfn, &stcompiled) == -1)
  {
    if (errno == ENOENT) return 0 ;
    strerr_warnfusys("stat ", dstfn) ;
    return -1 ;
  }

 /* Do we have a snapshot of the source set for the compiled db? */
  {
    struct stat stsnap ;
    char snap[repolen + 54 + setlen] ;
    ssize_t r ;
    memcpy(snap, srcfn, repolen + 9) ;
    memcpy(snap + repolen + 9, ".snapshot", 9) ;
    r = readlink(dstfn, snap + repolen + 18, setlen + 35) ;
    if (r == -1)
    {
      strerr_warnfusys("readlink ", dstfn) ;
      return -1 ;
    }
    if (r != setlen + 34)
    {
      strerr_warnf("invalid symlink: ", dstfn) ;
      return -1 ;
    }
    snap[repolen + setlen + 53] = 0 ;
    if (stat(snap, &stsnap) == -1)
    {
      if (errno == ENOENT) return 0 ;
      strerr_warnfusys("stat ", snap) ;
      return -1 ;
    }
  }

 /* Has the compiled db been built with the latest major s6-rc version? */
  {
    uint32_t version = s6rc_repo_read_major(dstfn) ;
    if (!version)
    {
      strerr_warnfusys("read major version from ", dstfn) ;
      return -1 ;
    }
    if (version < s6rc_get_major(S6RC_VERSION)) return 0 ;
  }

 /* Is the compiled db more recent than the source set? */
  return
    stsource.st_mtim.tv_sec < stcompiled.st_mtim.tv_sec ? 1 :
    stsource.st_mtim.tv_sec > stcompiled.st_mtim.tv_sec ? 0 :
    stsource.st_mtim.tv_nsec < stcompiled.st_mtim.tv_nsec ;
}
