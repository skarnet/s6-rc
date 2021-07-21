/* ISC license. */

#include <stdint.h>

#include <s6-rc/db.h>

s6rc_common_t const *s6rc_service_common (s6rc_db_t const *db, uint32_t id)
{
  uint32_t num ;
  uint8_t type ;
  s6rc_service_typenum(db->n, id, &type, &num) ;
  return s6rc_service_common_tn(db, type, num) ;
}
