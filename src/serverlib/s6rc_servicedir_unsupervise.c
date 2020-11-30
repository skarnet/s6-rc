/* ISC license. */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <skalibs/posixplz.h>
#include <s6/s6-supervise.h>
#include <s6-rc/s6rc-servicedir.h>

void s6rc_servicedir_unsupervise (char const *live, char const *prefix, char const *name, int keepsupervisor)
{
  size_t livelen = strlen(live) ;
  size_t prefixlen = strlen(prefix) ;
  size_t namelen = strlen(name) ;
  char fn[livelen + 14 + prefixlen + namelen] ;
  memcpy(fn, live, livelen) ;
  memcpy(fn + livelen, "/scandir/", 9) ;
  memcpy(fn + livelen + 9, prefix, prefixlen) ;
  memcpy(fn + livelen + 9 + prefixlen, name, namelen + 1) ;
  unlink_void(fn) ;
  if (!keepsupervisor)
  {
    int e = errno ;
    memcpy(fn + livelen + 1, "servicedirs/", 12) ;
    memcpy(fn + livelen + 13, name, namelen + 1) ;
    s6_svc_writectl(fn, S6_SUPERVISE_CTLDIR, "x", 1) ;
    errno = e ;
  }
}
