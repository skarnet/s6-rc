/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/repo.h>

static void cleanup (char const *s)
{
  int e = errno ;
  rm_rf(s) ;
  errno = e ;
}

int s6rc_repo_setcopy (char const *repo, char const *srcname, char const *dstname, unsigned int flags)
{
  size_t repolen = strlen(repo) ;
  size_t srclen = strlen(srcname) ;
  size_t dstlen = strlen(dstname) ;
  size_t r ;
  char src[repolen + srclen + 10] ;
  char dst[repolen + dstlen + 10] ;
  char realsrc[repolen + srclen + 18] ;
  char realdst[repolen + dstlen + 18] ;
  char olddst[repolen + dstlen + 18] ;
  memcpy(src, repo, repolen) ;
  memcpy(src + repolen, "/sources/", 9) ;
  memcpy(src + repolen + 9, srcname, srclen + 1) ;
  memcpy(dst, src, repolen + 9) ;
  memcpy(dst + repolen + 9, dstname, dstlen + 1) ;
  memcpy(olddst, dst, repolen + 9) ;
  olddst[repolen + 9] = 0 ;
  if (access(dst, F_OK) == -1)
  {
    if (errno != ENOENT) return errno ;
  }
  else if (!(flags & 1)) return -1 ;

  memcpy(realsrc, src, repolen + 9) ;
  r = readlink(src, realsrc + repolen + 9, srclen + 9) ;
  if (r == -1) return errno ;
  if (r != srclen + 8) return -2 ;
  realsrc[repolen + srclen + 17] = 0 ;
  memcpy(realdst, dst, repolen + 9) ;
  realdst[repolen + 9] = '.' ;
  memcpy(realdst + repolen + 10, dstname, dstlen) ;
  memcpy(realdst + repolen + 10 + dstlen, ":XXXXXX", 8) ;
  if (mkntemp(realdst) == -1) return errno ;
  if (!hiercopy(realsrc, realdst))
  {
    cleanup(realdst) ;
    return errno ;
  }
  if (!atomic_symlink4(realdst + repolen + 9, dst, olddst + repolen + 9, dstlen + 9))
  {
    cleanup(realdst) ;
    return errno ;
  }
  if (olddst[repolen + 9]) cleanup(olddst) ;
  return 0 ;
}
