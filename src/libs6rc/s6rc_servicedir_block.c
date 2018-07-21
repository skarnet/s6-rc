/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <skalibs/posixplz.h>
#include <skalibs/djbunix.h>
#include <s6/s6-supervise.h>
#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_block (char const *dir)
{
  size_t dirlen = strlen(dir) ;
  s6_svstatus_t status ;
  char fn[dirlen + 6] ;
  if (!s6_svstatus_read(dir, &status)) return -1 ;
  memcpy(fn, dir, dirlen) ;
  memcpy(fn + dirlen, "/down", 6) ;
  if (!touch(fn)) return -1 ;
  if (s6_svc_writectl(dir, S6_SUPERVISE_CTLDIR, "O", 1) < 0)
  {
    unlink_void(fn) ;
    return -1 ;
  }
  return status.flagwantup ;
}
