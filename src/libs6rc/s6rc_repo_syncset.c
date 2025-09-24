/* ISC license. */

#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

int s6rc_repo_syncset (char const *repo, char const *set, unsigned int verbosity)
{
  stralloc sa = STRALLOC_ZERO ;
  genalloc ga = GENALLOC_ZERO ;  /* size_t */
  int r = s6rc_repo_syncset_tmp(repo, set, &sa, &ga, verbosity) ;
  genalloc_free(&size_t, &ga) ;
  stralloc_free(&sa) ;
  return r ;
}
