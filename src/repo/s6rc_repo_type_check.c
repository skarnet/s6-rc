/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/repo.h>

int s6rc_repo_type_check (char const *src)
{
  ssize_t r ;
  char type[8] ;
  size_t srclen = strlen(src) ;
  char fn[srclen + 6] ;
  memcpy(fn, src, srclen) ;
  memcpy(fn + srclen, "/type", 6) ;
  r = openreadnclose(fn, type, 8) ;
  if (r == -1) return -1 ;
  if (r < 6) return 0 ;
  if (type[r-1] == '\n') r-- ;
  if (r == 8) return 0 ;
  type[r++] = 0 ;
  return
    !strcmp(type, "longrun") ? 1 :
    !strcmp(type, "oneshot") ? 2 :
    !strcmp(type, "bundle")  ? 3 : 0 ;
}
