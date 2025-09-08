/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_refcompile (char const *repo, char *oldc, unsigned int verbosity, char const *fdhuser)
{
  static char const *const subs[2] = { ".atomics", ".bundles" } ;
  return s6rc_repo_compile(repo, ".ref", subs, 2, oldc, verbosity, fdhuser) ;
}
