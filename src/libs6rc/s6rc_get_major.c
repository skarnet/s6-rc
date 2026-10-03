/* ISC license. */

#include <stdint.h>

#include <skalibs/uint16.h>

#include <s6-rc/s6rc-utils.h>

uint32_t s6rc_get_major (char const *s)
{
  size_t len = 0, l ;
  uint32_t acc = 0 ;
  uint16_t u ;
  l = uint16_scan(s + len, &u) ;
  if (!l || s[len + l++] != '.') return 0 ;
  len += l ; acc = acc << 16 | u ;
  l = uint16_scan(s + len, &u) ;
  if (!l || s[len + l++] != '.') return 0 ;
  len += l ; acc = acc << 16 | u ;
  return acc ;
}
