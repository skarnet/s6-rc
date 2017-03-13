/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <s6/s6-supervise.h>
#include <s6-rc/s6rc-servicedir.h>

void s6rc_servicedir_unsupervise (char const *live, char const *name, int keepsupervisor)
{
  size_t namelen = strlen(name) ;
  size_t livelen = strlen(live) ;
  char fn[livelen + 14 + namelen] ;
  memcpy(fn, live, livelen) ;
  memcpy(fn + livelen, "/scandir/", 9) ;
  memcpy(fn + livelen + 9, name, namelen + 1) ;
  unlink(fn) ;
  if (!keepsupervisor)
  {
    memcpy(fn + livelen + 1, "servicedirs/", 12) ;
    memcpy(fn + livelen + 13, name, namelen + 1) ;
    s6_svc_writectl(fn, S6_SUPERVISE_CTLDIR, "x", 1) ;
  }
}
