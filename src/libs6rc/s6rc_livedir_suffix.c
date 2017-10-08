/* ISC license. */

#include <string.h>
#include <errno.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>

ssize_t s6rc_livedir_suffix (char const *live, char *s, size_t n)
{
  size_t llen = strlen(live) ;
  size_t r ;
  char sfn[llen + 8] ;
  memcpy(sfn, live, llen) ;
  memcpy(sfn + llen, "/suffix", 8) ;
  r = openreadnclose(sfn, s, n) ;
  if (r < 0) return r ;
  if (memchr(s, '/', r)) return (errno = EINVAL, 0) ;
  return r ;
}
