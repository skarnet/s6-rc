/* ISC license. */

#include <string.h>

#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

int s6rc_repo_lock (char const *repo, int w)
{
  int fd ;
  size_t repolen = strlen(repo) ;
  char fn[repolen + 13] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/lock", 6) ;
  fd = openc_write(fn) ;
  if (fd == -1) return -1 ;
  if (fd_lock(fd, w, 0) < 1) return -1 ;
  return fd ;
}
