/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_listalldeps (char const *repo, char const *const *services, size_t n, stralloc *storage, genalloc *indices, int up)
{
  return s6rc_repo_listdeps_internal(repo, services, n, storage, indices, 2 | !!up) ;
}
