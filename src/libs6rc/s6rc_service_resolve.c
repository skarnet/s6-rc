/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/uint32.h>
#include <skalibs/bytestr.h>
#include <skalibs/cdb.h>

#include <s6-rc/db.h>

int s6rc_service_resolve (cdb_t *c, s6rc_sid_t *id, char const *s)
{
  size_t len = strlen(s) ;
  char const *param = 0 ;
  char pack[5] ;
  int r = cdb_find(c, s, len) ;
  if (r < 0) return r ;
  if (!r)
  {
    size_t at = byte_chr(s, len, '@') ;
    if (at == len) return 0 ;
    if (at == len - 1) return (errno = EINVAL, -1) ;
    r = cdb_find(c, s, at + 1) ;
    if (r <= 0) return r ;
    param = s + at + 1 ;
  }
  if (cdb_read(c, pack, 5, cdb_datapos(c)) < 0) return -1 ;
  id->stype = pack[0] ;
  uint32_unpack_big(pack + 1, &id->i) ;
  id->param = param ;
  return 1 ;
}
