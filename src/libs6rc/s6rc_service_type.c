/* ISC license. */

#include <stdint.h>

#include <s6-rc/db.h>

uint8_t s6rc_service_type (uint32_t const *dbn, uint32_t id)
{
  uint32_t num ;
  uint8_t type ;
  s6rc_service_typenum(dbn, id, &type, &num) ;
  return type ;
}
