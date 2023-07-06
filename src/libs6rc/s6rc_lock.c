/* ISC license. */

#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <skalibs/djbunix.h>

#include <s6-rc/s6rc-utils.h>

static inline int modefor (int what)
{
  return (what > 1 ? O_RDWR | O_CREAT | O_TRUNC : O_RDONLY) | O_NONBLOCK | O_CLOEXEC ;
}

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
    lfd = open3(lfn, modefor(lwhat), 0644) ;
    if (lfd < 0) return 0 ;
    r = fd_lock(lfd, lwhat > 1, !blocking) ;
    if (!r) errno = EBUSY ;
    if (r < 1) goto lerr ;
  }

  if (cwhat)
  {
    int r ;
    size_t clen = strlen(compiled) ;
    char cfn[clen + 6] ;
    memcpy(cfn, compiled, clen) ;
    memcpy(cfn + clen, "/lock", 6) ;
    cfd = open3(cfn, modefor(cwhat), 0644) ;
    if (cfd < 0) goto lerr ;
    r = fd_lock(cfd, cwhat > 1, !blocking) ;
    if (!r) errno = EBUSY ;
    if (r < 1) goto cerr ;
    *ccfd = cfd ;
  }

  if (lwhat) *llfd = lfd ;
  return 1 ;

 cerr:
  fd_close(cfd) ;
 lerr:
  if (lwhat) fd_close(lfd) ;
  return 0 ;
}
