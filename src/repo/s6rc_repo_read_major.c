/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <skalibs/uint32.h>
#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

#include <skalibs/posixishard.h>

uint32_t s6rc_repo_read_major (char const *compiled)
{
  size_t clen = strlen(compiled) ;
  ssize_t r ;
  uint32_t res ;
  char pack[5] ;
  char fn[clen + 16] ;
  memcpy(fn, compiled, clen) ;
  memcpy(fn + clen, "/metainfo/major", 16) ;
  r = openreadnclose(fn, pack, 5) ;
  if (r != 4) return r == -1 && errno == ENOENT ? 7 : 0 ; /* up to 0.7.0.0 doesn't have metainfo */
  uint32_unpack_big(pack, &res) ;
  return res ;
}
