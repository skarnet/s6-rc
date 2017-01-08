/* ISC license. */

#include <sys/types.h>
#include <unistd.h>
#include <skalibs/bytestr.h>
#include <s6/s6-supervise.h>
#include <s6-rc/s6rc-servicedir.h>

void s6rc_servicedir_unsupervise (char const *live, char const *name, int keepsupervisor)
{
  size_t namelen = str_len(name) ;
  size_t livelen = str_len(live) ;
  char fn[livelen + 14 + namelen] ;
  byte_copy(fn, livelen, live) ;
  byte_copy(fn + livelen, 9, "/scandir/") ;
  byte_copy(fn + livelen + 9, namelen + 1, name) ;
  unlink(fn) ;
  if (!keepsupervisor)
  {
    byte_copy(fn + livelen + 1, 12, "servicedirs/") ;
    byte_copy(fn + livelen + 13, namelen + 1, name) ;
    s6_svc_writectl(fn, S6_SUPERVISE_CTLDIR, "x", 1) ;
  }
}
