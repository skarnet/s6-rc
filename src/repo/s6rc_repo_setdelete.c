/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

void s6rc_repo_setdelete (char const *repo, char const *set)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  ssize_t r ;
  char real[repolen + setlen + 18] ;
  char fn[repolen + setlen + 11] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, set, setlen + 1) ;
  if (access(fn, W_OK) == -1 && errno != ENOENT) return ;
  memcpy(real, repo, repolen) ;
  memcpy(real + repolen, "/sources/", 9) ;
  r = readlink(fn, real + repolen + 9, setlen + 9) ;
  if (r == -1 || r != setlen + 8) return ;
  real[repolen + setlen + 17] = 0 ;
  unlink_void(fn) ;
  rm_rf(real) ;
}
