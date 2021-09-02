/* ISC license. */

#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <s6/supervise.h>

#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_unblock (char const *dir, int h)
{
  if (h)
  {
    size_t dirlen = strlen(dir) ;
    char fn[dirlen + 6] ;
    memcpy(fn, dir, dirlen) ;
    memcpy(fn + dirlen, "/down", 6) ;
    if (unlink(fn) < 0 && errno != ENOENT) return -1 ;
    if (s6_svc_writectl(dir, S6_SUPERVISE_CTLDIR, "u", 1) < 0) return -1 ;
  }
  return 0 ;
}
