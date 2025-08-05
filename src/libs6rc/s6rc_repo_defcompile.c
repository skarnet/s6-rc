/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_defcompile (char const *repo, char const *set, char const *bundle, char *oldc, unsigned int verbosity, char const *fdhuser)
{
  static char const *const subs[3] = { "active", "onboot", "bbuild" } ;
  if (!s6rc_repo_makebbuild(repo, set)) return -1 ;
  if (!s6rc_repo_makedefbundle(repo, set, bundle)) return -1 ;
  return s6rc_repo_compile(repo, set, subs, 3, oldc, verbosity, fdhuser) ;
}
