/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/bytestr.h>
#include <skalibs/direntry.h>
#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

int s6rc_repo_fillset (char const *repo, char const *set, char const *const *existing, uint32_t n)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  DIR *dir ;
  char atomics[repolen + 18] ;
  char setfn[repolen + 10 + setlen] ;
  memcpy(atomics, repo, repolen) ;
  memcpy(atomics + repolen, "/sources/.atomics", 18) ;
  memcpy(setfn, repo, repolen) ;
  memcpy(setfn + repolen, "/sources/", 9) ;
  memcpy(setfn + repolen + 9, set, setlen+1) ;

  dir = opendir(atomics) ;
  if (!dir)
  {
    strerr_warnfu2sys("opendir ", atomics) ;
    return 0 ;
  }
  for (;;)
  {
    size_t len ;
    uint32_t flags ;
    direntry *d ;
    uint8_t subi ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;

    len = strlen(d->d_name) ;
    char src[len + 16] ;
    char dst[repolen + 18 + setlen + len] ;
    memcpy(src, "../../.atomics/", 15) ;
    memcpy(src + 15, d->d_name, len+1) ;
    if (n && bsearch(d->d_name, existing, n, sizeof(char const *), &str_bcmp)) continue ;
    if (s6rc_repo_getserviceflags(repo, d->d_name, &flags) <= 0) return 0 ;
    if (flags & 1) subi = 3 ;
    else if (flags & 2) subi = 2 ;
    else subi = 1 ;
    memcpy(dst, setfn, repolen + 9 + setlen) ;
    dst[repolen + 9 + setlen] = '/' ;
    memcpy(dst + repolen + 10 + setlen, s6rc_repo_subnames[subi], 6) ;
    dst[repolen + setlen + 16] = '/' ;
    memcpy(dst + repolen + setlen + 17, d->d_name, len+1) ;
    if (symlink(src, dst) == -1)
    {
      strerr_warnfu4sys("symlink ", src, " to ", dst) ;
      return 0 ;
    }
  }
  dir_close(dir) ;
  if (errno)
  {
    strerr_warnfu2sys("readdir ", atomics) ;
    return 0 ;
  }

  return 1 ;
}
