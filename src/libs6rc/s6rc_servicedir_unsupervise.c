/* ISC license. */

#include <string.h>

#include <s6/supervise.h>

#include <s6-rc/s6rc-servicedir.h>

void s6rc_servicedir_unsupervise (char const *live, char const *prefix, char const *name, int keepsupervisor)
{
  size_t livelen = strlen(live) ;
  size_t prefixlen = strlen(prefix) ;
  size_t namelen = strlen(name) ;
  char scdir[livelen + 9] ;
  char fn[prefixlen + namelen + 1] ;
  memcpy(scdir, live, livelen) ;
  memcpy(scdir + livelen, "/scandir", 9) ;
  memcpy(fn, prefix, prefixlen) ;
  memcpy(fn + prefixlen, name, namelen + 1) ;
  s6_supervise_unlink(scdir, fn, keepsupervisor ? 0 : 3) ;
}
