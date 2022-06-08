/* ISC license. */

#include <string.h>
#include <errno.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>

ssize_t s6rc_livedir_prefix (char const *live, char *s, size_t n)
{
  size_t llen = strlen(live) ;
  ssize_t r ;
  char sfn[llen + 8] ;
  memcpy(sfn, live, llen) ;
  memcpy(sfn + llen, "/prefix", 8) ;
  r = openreadnclose(sfn, s, n) ;
  if (r == -1) return errno == ENOENT ? 0 : r ;
  if (memchr(s, '/', r) || memchr(s, '\n', r)) return (errno = EINVAL, -1) ;
  return r ;
}
