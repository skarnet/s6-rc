/* ISC license. */

#include <stdint.h>

#include <s6-rc/db.h>

void s6rc_service_typenum (uint32_t const *db, uint32_t id, uint8_t *type, uint32_t *num)
{
  uint8_t t = 0 ;
  while (id >= dbn[t]) id -= dbn[t++] ;
  *type = t ;
  *num = id ;
}
