/* ISC license. */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <skalibs/bytestr.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>
#include "s6rc-servicedir-internal.h"
#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_copy_online (char const *src, char const *dst)
{
  unsigned int srclen = str_len(src) ;
  unsigned int dstlen = str_len(dst) ;
  unsigned int n ;
  unsigned int i = 0 ;
  int wantup = 0 ;
  int e ;
  char srcfn[srclen + s6rc_servicedir_file_maxlen + 6] ;
  char dstfn[dstlen + s6rc_servicedir_file_maxlen + 6] ;
  char oldfn[dstlen + s6rc_servicedir_file_maxlen + 6] ;
  byte_copy(srcfn, srclen, src) ;
  srcfn[srclen] = '/' ;
  byte_copy(dstfn, dstlen + 1, dst) ;
  byte_copy(oldfn, dstlen, dst) ;
  byte_copy(oldfn + dstlen, 5, "/old") ;
  if (rm_rf(oldfn) < 0 && errno != ENOENT) return 0 ;
  if (mkdir(oldfn, 0755) < 0) return 0 ;
  dstfn[dstlen] = '/' ;
  oldfn[dstlen + 4] = '/' ;
  wantup = s6rc_servicedir_block(dst) ;
  if (wantup < 0) { e = errno ; goto errdir ; }

  for (; s6rc_servicedir_file_list[i].name ; i++)
  {
    str_copy(dstfn + dstlen + 1, s6rc_servicedir_file_list[i].name) ;
    str_copy(oldfn + dstlen + 5, s6rc_servicedir_file_list[i].name) ;
    if (rename(dstfn, oldfn) < 0
     && (errno != ENOENT || s6rc_servicedir_file_list[i].options & SVFILE_MANDATORY))
    {
      e = errno ;
      goto errrename ;
    }
  }
  n = i ;
  while (i--)
  {
    if (!s6rc_servicedir_copy_one(src, dst, s6rc_servicedir_file_list + i))
    {
      e = errno ;
      goto errremove ;
    }
  }
  oldfn[dstlen + 4] = 0 ;
  rm_rf(oldfn) ;
  s6rc_servicedir_unblock(dst, wantup) ;
  return 1 ;

 errremove:
  for (; i < n ; i++)
  {
    str_copy(dstfn + dstlen + 1, s6rc_servicedir_file_list[i].name) ;
    rm_rf(dstfn) ;
  }
  i = n ;
 errrename:
  while (i--)
  {
    str_copy(dstfn + dstlen + 1, s6rc_servicedir_file_list[i].name) ;
    str_copy(oldfn + dstlen + 5, s6rc_servicedir_file_list[i].name) ;
    rename(oldfn, dstfn) ;
  }
 errdir:
  oldfn[dstlen + 4] = 0 ;
  rmdir(oldfn) ;
  errno = e ;
  return 0 ;
}
