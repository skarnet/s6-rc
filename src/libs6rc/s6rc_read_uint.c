/* ISC license. */

#include <sys/types.h>
#include <errno.h>
#include <skalibs/types.h>
#include <skalibs/bytestr.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>

int s6rc_read_uint (char const *file, unsigned int *u)
{
  char buf[UINT_FMT + 1] ;
  ssize_t r = openreadnclose(file, buf, UINT_FMT) ;
  if (r < 0) return (errno == ENOENT) ? 0 : -1 ;
  buf[byte_chr(buf, r, '\n')] = 0 ;
  if (!uint0_scan(buf, u)) return (errno = EINVAL, -1) ;
  return 1 ;
}
