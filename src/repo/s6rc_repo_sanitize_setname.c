/* ISC license. */

#include <string.h>

#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

void s6rc_repo_sanitize_setname (char const *set)
{
  if (!set[0])
    strerr_dief2x(100, "set names cannot ", "be empty") ;
  if (strchr(set, '/') || strchr(set, '\n'))
    strerr_dief2x(100, "set names cannot ", "contain / or newlines") ;
}
