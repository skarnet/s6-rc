/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_setcompile (char const *repo, char const *set, char const *defbundle, char *oldc, unsigned int verbosity, char const *fdhuser)
{
  static char const *const subs[4] = { "active", "onboot", "always", "bundle" } ;
  if (!s6rc_repo_makesetbundles(repo, set, verbosity)) return -1 ;
  if (!s6rc_repo_makedefbundle(repo, set, defbundle)) return -1 ;
  return s6rc_repo_compile(repo, set, subs, 4, oldc, verbosity, fdhuser) ;
}
