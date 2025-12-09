/* ISC license. */

#include <string.h>

#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

void s6rc_repo_removeinternals (genalloc *ga, unsigned int start, char const *s)
{
  size_t *indices = genalloc_s(size_t, ga) ;
  unsigned int n = genalloc_len(size_t, ga) ;
  for (unsigned int i = start ; i < n ; i++)
    if (!strncmp(s + indices[i], "s6rc-", 5) || !strncmp(s + indices[i], "s6-rc-", 6))
      indices[i--] = indices[--n] ;
  genalloc_setlen(size_t, ga, n) ;
}
