/* ISC license. */

#include <string.h>

#include <skalibs/uint32.h>
#include <skalibs/bytestr.h>
#include <skalibs/cdb.h>

#include <s6-rc/db.h>

int s6rc_service_resolve (s6rc_db_t const *db, char const *s, uint32_t *id, char const **param)
{
  size_t len = strlen(s) ;
  char const *p = 0 ;
  cdb_reader reader = CDB_READER_ZERO ;
  cdb_data data ;
  int r = cdb_find(c, &reader, &data, s, len) ;
  if (r < 0) return r ;
  else if (!r)
  {
    size_t at = byte_chr(s, len, '@') ;
    if (at == len) return 0 ;
    if (at == len - 1) return -1 ;
    cdb_findstart(&reader) ;
    r = cdb_find(c, &reader, &data, s, at + 1) ;
    if (r <= 0) return r ;
    p = s + at + 1 ;
  }
  if (data.len != 4) return -1 ;
  uint32_unpack_big(data.s, id) ;
  *param = p ;
  return 1 ;
}
