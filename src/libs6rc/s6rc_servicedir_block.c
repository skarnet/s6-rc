/* ISC license. */

#include <s6/supervise.h>

#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_block (char const *dir)
{
  s6_svstatus_t status ;
  if (!s6_svstatus_read(dir, &status)) return -1 ;
  if (s6_svc_writectl(dir, S6_SUPERVISE_CTLDIR, "Q", 1) < 0) return -1 ;
  return status.flagwantup ;
}
