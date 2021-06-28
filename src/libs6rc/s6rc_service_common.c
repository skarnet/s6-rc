/* ISC license. */

#include <stdint.h>

#include <s6-rc/db.h>

s6rc_common_t const *s6rc_service_common (s6rc_db_t const *db, s6rc_sid_t const *id)
{
  uint32_t i = id->i + (id->param ? db->n[id->stype] : 0) ;
  switch (id->stype)
  {
    case STYPE_LONGRUN :  return &db->longruns[i].common ;
    case STYPE_ONESHOT :  return &db->oneshots[i].common ;
    case STYPE_EXTERNAL : return &db->externals[i].common ;
    case STYPE_BUNDLE :   return &db->bundles[i].common ;
    case STYPE_VIRTUAL :  return &db->virtuals[i].common ;
    default : return 0 ;
  }
}
