/* ISC license. */

#include <string.h>

#include <s6-rc/repo.h>

int s6rc_repo_list_sets (char const *repo, stralloc *sa, genalloc *ga)
{
  size_t repolen = strlen(repo) ;
  char fn[repolen + 9] ;
  memcpy(fn, repo, repolen) ;
  memcpy(fn + repolen, "/sources", 9) ;
  return s6rc_repo_ls(fn, sa, ga) ;
}
