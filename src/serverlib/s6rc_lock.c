/* ISC license. */

#include <string.h>
#include <errno.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>

static inline int lockex (int fd, int blocking)
{
  return blocking ? lock_ex(fd) : lock_exnb(fd) ;
}

static inline int locksh (int fd, int blocking)
{
  return blocking ? lock_sh(fd) : lock_shnb(fd) ;
}

int s6rc_lock (char const *live, int lwhat, int *llfd, char const *compiled, int cwhat, int *ccfd, int blocking)
{
  int lfd = -1, cfd = -1 ;

  if (lwhat)
  {
    size_t llen = strlen(live) ;
    char lfn[llen + 6] ;
    memcpy(lfn, live, llen) ;
    memcpy(lfn + llen, "/lock", 6) ;
    lfd = open_create(lfn) ;
    if (lfd < 0) return 0 ;
    if (coe(lfd) < 0) goto lerr ;
    if ((lwhat > 1 ? lockex(lfd, blocking) : locksh(lfd, blocking)) < 0) goto lerr ;
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
      if (coe(cfd) < 0) goto cerr ;
      if ((cwhat > 1 ? lockex(cfd, blocking) : locksh(cfd, blocking)) < 0) goto cerr ;
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
