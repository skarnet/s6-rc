/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/alloc.h>

#include <s6-rc/s6rc-utils.h>

int s6rc_livedir_canon (char const **live)
{
  size_t llen = strlen(*live) ;
  size_t n = llen ;
  while (n && (*live)[n - 1] == '/') --n ;
  if (!n) return (errno = EINVAL, 0) ;
  if (n < llen)
  {
    char *x = alloc(n + 1) ;
    if (!x) return 0 ;
    memcpy(x, *live, n) ;
    x[n] = 0 ;
    *live = x ;
  }
  return 1 ;
}
