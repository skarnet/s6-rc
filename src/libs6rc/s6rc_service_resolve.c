/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/uint32.h>
#include <skalibs/bytestr.h>
#include <skalibs/cdb.h>

#include <s6-rc/db.h>

int s6rc_service_resolve (cdb_t *c, char const *s, uint32_t *id, char const **param)
{
  size_t len = strlen(s) ;
  char const *p = 0 ;
  int r = cdb_find(c, s, len) ;
  if (r < 0) return r ;
  if (!r)
  {
    size_t at = byte_chr(s, len, '@') ;
    if (at == len) return 0 ;
    if (at == len - 1) return (errno = EINVAL, -1) ;
    r = cdb_find(c, s, at + 1) ;
    if (r <= 0) return r ;
    p = s + at + 1 ;
  }
  uint32_unpack_big(cdb_datapos(c), id) ;
  *param = p ;
  return 1 ;
}
