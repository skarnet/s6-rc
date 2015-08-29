/* ISC license. */

#include <errno.h>
#include <skalibs/bytestr.h>
#include <skalibs/diuint.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>

int s6rc_lock (char const *live, int lwhat, int *llfd, char const *compiled, int cwhat, int *ccfd)
{
  int e ;
  int lfd = -1, cfd = -1 ;

  if (lwhat)
  {
    unsigned int llen = str_len(live) ;
    char lfn[llen + 6] ;
    byte_copy(lfn, llen, live) ;
    byte_copy(lfn + llen, 6, "/lock") ;
    lfd = open_create(lfn) ;
    if (lfd < 0) return 0 ;
    if ((lwhat > 1 ? lock_ex(lfd) : lock_sh(lfd)) < 0) { e = errno ; goto lerr ; }
  }

  if (cwhat)
  {
    unsigned int clen = str_len(compiled) ;
    char cfn[clen + 6] ;
    byte_copy(cfn, clen, compiled) ;
    byte_copy(cfn + clen, 6, "/lock") ;
    cfd = open_create(cfn) ;
    if (cfd < 0)
      if (cwhat == 1 || errno != EROFS) { e = errno ; goto lerr ; }
      else cfd = -errno ;
    else if ((cwhat > 1 ? lock_ex(cfd) : lock_sh(cfd)) < 0) { e = errno ; goto cerr ; }
  }

  if (lwhat) *llfd = lfd ;
  if (cwhat) *ccfd = cfd ;
  return 1 ;

 cerr:
  fd_close(cfd) ;
 lerr:
  if (lwhat) fd_close(lfd) ;
  errno = e ;
  return 0 ;
}
