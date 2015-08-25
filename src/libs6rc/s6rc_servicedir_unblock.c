/* ISC license. */

#include <errno.h>
#include <unistd.h>
#include <skalibs/bytestr.h>
#include <s6/s6-supervise.h>
#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_unblock (char const *dir, int h)
{
  if (h)
  {
    unsigned int dirlen = str_len(dir) ;
    char fn[dirlen + 6] ;
    byte_copy(fn, dirlen, dir) ;
    byte_copy(fn + dirlen, 6, "/down") ;
    if (unlink(fn) < 0 && errno != ENOENT) return -1 ;
    if (s6_svc_writectl(dir, S6_SUPERVISE_CTLDIR, "u", 1) < 0) return -1 ;
  }
  return 0 ;
}
