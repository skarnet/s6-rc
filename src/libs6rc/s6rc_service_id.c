/* ISC license. */

#include <stdint.h>

#include <s6-rc/db.h>

uint32_t s6rc_service_id (uint32_t const *dbn, uint8_t type, uint32_t num)
{
  uint32_t acc = 0 ;
  for (uint8_t t = 0 ; t < type ; t++) acc += dbn[t] ;
  return acc + num ;
}
