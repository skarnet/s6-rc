/* ISC license. */

#include <string.h>

#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

void s6rc_repo_sanitize_svname (char const *sv)
{
  if (!sv[0])
    strerr_dief2x(100, "service names cannot ", "be empty") ;
  if (sv[0] == '.')
    strerr_dief3x(100, "service names cannot ", "start with ", "a dot") ;
  if (strchr(sv, '/') || strchr(sv, '\n'))
    strerr_dief2x(100, "service names cannot ", "contain / or newlines") ;
  if (!strncmp(sv, "s6rc-", 5) || !strncmp(sv, "s6-rc-", 6))
    strerr_dief3x(100, "service names cannot ", "start with ", "reserved prefixes") ;
}
