/* ISC license. */

#include <string.h>

#include <s6-rc/repo.h>

int s6rc_repo_listsub (char const *repo, char const *set, char const *sub, stralloc *sa, genalloc *ga)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t sublen = strlen(sub) ;
  char fn[repolen + setlen + 11 + sublen] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources/", 9) ;
  memcpy(fn + repolen + 9, set, setlen) ;
  fn[repolen + 9 + setlen] = '/' ;
  memcpy(fn + repolen + 10 + setlen, sub, sublen + 1) ;

  return s6rc_repo_ls(fn, sa, ga) ;
}
