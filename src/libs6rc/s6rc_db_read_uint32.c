/* ISC license. */

#include <skalibs/uint32.h>
#include <skalibs/buffer.h>
#include <s6-rc/s6rc-db.h>

int s6rc_db_read_uint32 (buffer *b, uint32 *x)
{
  unsigned int w = 0 ;
  char pack[4] ;
  if (buffer_getall(b, pack, 4, &w) <= 0) return 0 ;
  uint32_unpack_big(pack, x) ;
  return 1 ;
}
