/* ISC license. */

#include <s6-rc/db.h>

s6rc_common_t const *s6rc_service_common_tn (s6rc_db_t const *db, uint8_t type, uint32_t num)
{
  switch (type)
  {
    case STYPE_LONGRUN : return &db->longruns[num].common ;
    case S6RC_STYPE_N + STYPE_LONGRUN : return &db->longruns[db->n[S6RC_STYPE_LONGRUN] + num].common ;
    case STYPE_ONESHOT : return &db->oneshots[num].common ;
    case S6RC_STYPE_N + STYPE_ONESHOT : return &db->oneshots[db->n[S6RC_STYPE_ONESHOT] + num].common ;
    case STYPE_EXTERNAL : return &db->externals[num].common ;
    case S6RC_STYPE_N + STYPE_EXTERNAL : return &db->externals[db->n[S6RC_STYPE_EXTERNAL] + num].common ;
    case STYPE_BUNDLE : return &db->bundles[num].common ;
    case S6RC_STYPE_N + STYPE_BUNDLE : return &db->bundles[db->n[S6RC_STYPE_BUNDLE] + num].common ;
    case STYPE_VIRTUAL : return &db->virtuals[num].common ;
    case S6RC_STYPE_N + STYPE_VIRTUAL : return &db->virtuals[db->n[S6RC_STYPE_VIRTUAL] + num].common ;
    default : return 0 ;
  }
}
