/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_set_defcompile (char const *repo, char const *set, char const *bundle, char *oldc, unsigned int verbosity, char const *fdhuser)
{
  static char const *const subs[3] = { "active", "onboot", "bundle" } ;
  if (!s6rc_repo_set_makebundle(repo, set, "onboot", bundle)) return -1 ;
  return s6rc_repo_set_compile(repo, set, subs, 3, oldc, verbosity, fdhuser) ;
}
