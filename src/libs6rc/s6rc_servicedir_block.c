/* ISC license. */

#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <skalibs/bytestr.h>
#include <skalibs/djbunix.h>
#include <s6/s6-supervise.h>
#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_block (char const *dir)
{
  size_t dirlen = str_len(dir) ;
  s6_svstatus_t status ;
  char fn[dirlen + 6] ;
  if (!s6_svstatus_read(dir, &status)) return -1 ;
  byte_copy(fn, dirlen, dir) ;
  byte_copy(fn + dirlen, 6, "/down") ;
  if (!touch(fn)) return -1 ;
  if (s6_svc_writectl(dir, S6_SUPERVISE_CTLDIR, "O", 1) < 0)
  {
    register int e = errno ;
    unlink(fn) ;
    errno = e ;
    return -1 ;
  }
  return status.flagwantup ;
}
