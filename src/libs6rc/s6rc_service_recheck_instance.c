/* ISC license. */

#include <string.h>

#include <skalibs/uint32.h>
#include <skalibs/cdb.h>

#include <s6-rc/db.h>

int s6rc_service_recheck_instance (s6rc_db_t const *db, uint32_t *id, char const **param)
{
  uint32_t num ;
  uint8_t type ;
  s6rc_service_typenum(db->n, *id) ;
  if (type >= S6RC_STYPE_N)
  {
    s6rc_common_t const *common = s6rc_service_common(db->n, type, num) ;
    size_t namelen = strlen(db->storage + common->name) ;
    size_t paramlen = strlen(*param) ;
    cdb_data data ;
    int r ;
    char tmp[namelen + paramlen] ;
    memcpy(tmp, db->storage + common->name, namelen) ;
    memcpy(tmp + namelen, *param, paramlen) ;
    r = cdb_find(c, &data, tmp, namelen + paramlen) ;
    if (r <= 0) return r ;
    if (data.len != 4) return -1 ;
    uint32_unpack_big(data.s, id) ;
    *param = 0 ;
    return 1 ;
  }
  else return 0 ;
}
