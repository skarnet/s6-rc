/* ISC license. */

#include <s6/supervise.h>

#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_unblock (char const *dir, int h)
{
  if (h && s6_svc_writectl(dir, S6_SUPERVISE_CTLDIR, "U", 1) < 0) return -1 ;
  return 0 ;
}
