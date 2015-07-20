/* ISC license. */

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <skalibs/bytestr.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <s6/s6-supervise.h>
#include <s6-rc/s6rc-constants.h>

#define USAGE "s6-rc-update [ -l live ]"
#define dieusage() strerr_dieusage(100, USAGE)

static char const *live = S6RC_LIVE_BASE ;

static int safe_servicedir_update (char const *dst, char const *src, int h)
{
  unsigned int dstlen = str_len(dst) ;
  unsigned int srclen = str_len(src) ;
  int fd ;
  int hasdata = 1, hasenv = 1, ok = 1 ;
  char dstfn[dstlen + 15 + sizeof(S6_SUPERVISE_CTLDIR)] ;
  char srcfn[srclen + 17] ;
  char tmpfn[dstlen + 21] ;
  byte_copy(dstfn, dstlen, dst) ;
  byte_copy(srcfn, srclen, src) ;

  byte_copy(dstfn + dstlen, 6, "/down") ;
  fd = open_trunc(dstfn) ;
  if (fd < 0) strerr_warnwu2sys("touch ", dstfn) ;
  else close(fd) ;
  byte_copy(dstfn + dstlen + 1, 5, "data") ;
  byte_copy(tmpfn, dstlen + 5, dstfn) ;
  byte_copy(tmpfn + dstlen + 5, 5, ".old") ;
  if (rename(dstfn, tmpfn) < 0)
  {
    if (errno == ENOENT) hasdata = 0 ;
    else goto err ;
  }
  byte_copy(dstfn + dstlen + 1, 4, "env") ;
  byte_copy(tmpfn + dstlen + 1, 8, "env.old") ;
  if (rename(dstfn, tmpfn) < 0)
  {
    if (errno == ENOENT) hasenv = 0 ;
    else goto err ;
  }
  byte_copy(dstfn + dstlen + 1, 9, "nosetsid") ;
  if (unlink(dstfn) < 0 && errno != ENOENT) goto err ;
  byte_copy(dstfn + dstlen + 3, 14, "tification-fd") ;
  if (unlink(dstfn) < 0 && errno != ENOENT) goto err ;

  byte_copy(srcfn + srclen, 17, "/notification-fd") ;
  hiercopy(srcfn, dstfn) ;
  byte_copy(srcfn + srclen + 3, 7, "setsid") ;
  byte_copy(dstfn + dstlen + 3, 7, "setsid") ;
  hiercopy(srcfn, dstfn) ;
  byte_copy(srcfn + srclen + 1, 5, "data") ;
  byte_copy(dstfn + dstlen + 1, 5, "data") ;
  hiercopy(srcfn, dstfn) ;
  byte_copy(srcfn + srclen + 1, 4, "env") ;
  byte_copy(dstfn + dstlen + 1, 4, "env") ;
  hiercopy(srcfn, dstfn) ;
  byte_copy(srcfn + srclen + 1, 4, "run") ;
  byte_copy(dstfn + dstlen + 1, 4, "run") ;
  if (!hiercopy(srcfn, dstfn)) goto err ;
  byte_copy(srcfn + srclen + 1, 4, "run") ;
  byte_copy(dstfn + dstlen + 1, 4, "finish") ;
  hiercopy(srcfn, dstfn) ;
  if (h)
  {
    byte_copy(dstfn + dstlen + 1, 5, "down") ;
    if (unlink(dstfn) < 0)
    {
      strerr_warnwu2sys("unlink ", dstfn) ;
      ok = 0 ;
    }
    byte_copy(dstfn + dstlen + 1, 8 + sizeof(S6_SUPERVISE_CTLDIR), S6_SUPERVISE_CTLDIR "/control") ;
    s6_svc_write(dstfn, "u", 1) ;
  }
  byte_copy(dstfn + dstlen + 1, 9, "data.old") ;
  rm_rf(dstfn) ;
  byte_copy(dstfn + dstlen + 1, 8, "env.old") ;
  rm_rf(dstfn) ;
  return 1 ;

 err:
  if (h)
  {
    int e = errno ;
    byte_copy(dstfn + dstlen + 1, 5, "down") ;
    unlink(dstfn) ;
    errno = e ;
  }
  return 0 ;
}

static int servicedir_name_change (char const *live, char const *oldname, char const *newname)
{
  return 1 ;
} 

int main (int argc, char const *const *argv)
{
  PROG = "s6-rc-update" ;
  strerr_dief1x(100, "this utility has not been written yet.") ;
}
