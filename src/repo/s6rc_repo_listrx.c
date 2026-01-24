/* ISC license. */

#include <string.h>

#include <s6-rc/repo.h>

int s6rc_repo_listrx (char const *repo, char const *set, char const *rx, stralloc *sa, genalloc *ga)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t rxlen = strlen(rx) ;
  char fn[repolen + setlen + 11 + rxlen] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, set, setlen) ;
  fn[repolen + 9 + setlen] = '/' ;
  memcpy(fn + repolen + 10 + setlen, rx, rxlen + 1) ;

  return s6rc_repo_ls(fn, sa, ga) ;
}
