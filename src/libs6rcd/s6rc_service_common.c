/* ISC license. */

#include "db.h"

s6rc_common_t const *s6rc_service_common (s6rc_db_t const *db, s6rc_id_t id)
{
  switch (stype(id))
  {
    case STYPE_LONGRUN :  return &db->longruns[snum(id)].common ;
    case STYPE_ONESHOT :  return &db->oneshots[snum(id)].common ;
    case STYPE_EXTERNAL : return &db->externals[snum(id)].common ;
    case STYPE_BUNDLE :   return &db->bundles[snum(id)].common ;
    case STYPE_VIRTUAL :  return &db->virtuals[snum(id)].common ;
    default : return 0 ;
  }
}
