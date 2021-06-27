/* ISC license. */

#include "service.h"

common_t const *service_common (db_t const *db, stype_t stype, uint32_t i)
{
  if (stype >= STYPE_PHAIL) return 0 ;
  if (i >= db->n[stype]) return 0 ;
  switch (stype)
  {
    case STYPE_LONGRUN :  return &db->longruns[i].common ; 
    case STYPE_ONESHOT :  return &db->oneshots[i].common ; 
    case STYPE_EXTERNAL : return &db->externals[i].common ; 
    case STYPE_BUNDLE :   return &db->bundles[i].common ; 
    case STYPE_VIRTUAL :  return &db->virtuals[i].common ;
    default : return 0 ; 
  }
}
