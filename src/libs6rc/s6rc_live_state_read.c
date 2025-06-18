/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <skalibs/djbunix.h>

#include <s6-rc/s6rc-db.h>
#include <s6-rc/s6rc-utils.h>

int s6rc_live_state_read (char const *live, unsigned char *state, uint32_t n)
{
  size_t livelen = strlen(live) ;
  char fn[livelen + 7] ;
  memcpy(fn, live, livelen) ;
  memcpy(fn + livelen, "/state", n) ;
  ssize_t r = openreadnclose(fn, (char *)state, n) ;
  if (r == -1) return 0 ;
  if (r < n) return (errno = EINVAL, 0) ;
  for (uint32_t i = 0 ; i < n ; i++) state[i] &= 1 ;
  return 1 ;
}
