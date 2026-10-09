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
  char tmp[UINT32_FMT] ;
  char fn[clen + 7] ;
  memcpy(fn, compiled, clen) ;
  memcpy(fn + clen, "/major", 7) ;
  r = openreadnclose(fn, tmp, UINT32_FMT) ;
  if (r == -1) return errno == ENOENT ? 7 : 0 ; /* up to 0.7.0.0 doesn't have db/major */
  if (!r) return 0 ;
  if (tmp[r] != '\n') return (errno = EPROTO, 0) ;
  tmp[r] = 0 ;
  if (uint320_scan(tmp, &res) != r) return (errno = EPROTO, 0) ;
  return res ;
}
