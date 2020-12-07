/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/djbunix.h>

#include <s6-rc/s6rc-utils.h>

int s6rc_lock (char const *live, int lwhat, int *llfd, char const *compiled, int cwhat, int *ccfd, int blocking)
{
  int lfd = -1, cfd = -1 ;

  if (lwhat)
  {
    int r ;
    size_t llen = strlen(live) ;
    char lfn[llen + 6] ;
    memcpy(lfn, live, llen) ;
    memcpy(lfn + llen, "/lock", 6) ;
    lfd = open_create(lfn) ;
    if (lfd < 0) return 0 ;
    if (coe(lfd) < 0) goto lerr ;
    r = fd_lock(lfd, lwhat > 1, !blocking) ;
    if (!r) errno = EBUSY ;
    if (r < 1) goto lerr ;
  }

  if (cwhat)
  {
    size_t clen = strlen(compiled) ;
    char cfn[clen + 6] ;
    memcpy(cfn, compiled, clen) ;
    memcpy(cfn + clen, "/lock", 6) ;
    cfd = open_create(cfn) ;
    if (cfd < 0)
      if (cwhat > 1 || errno != EROFS) goto lerr ;
      else cfd = -errno ;
    else
    {
      int r ;
      if (coe(cfd) < 0) goto cerr ;
      r = fd_lock(cfd, cwhat > 1, !blocking) ;
      if (!r) errno = EBUSY ;
      if (r < 1) goto cerr ;
    }
  }

  if (lwhat) *llfd = lfd ;
  if (cwhat) *ccfd = cfd ;
  return 1 ;

 cerr:
  fd_close(cfd) ;
 lerr:
  if (lwhat) fd_close(lfd) ;
  return 0 ;
}
