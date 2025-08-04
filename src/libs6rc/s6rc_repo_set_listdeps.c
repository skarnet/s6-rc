/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_set_listdeps (char const *repo, char const *service, stralloc *storage, genalloc *indices, int up)
{
  return s6rc_repo_set_listdeps_internal(repo, &service, 1, storage, indices, !!up) ;
}
