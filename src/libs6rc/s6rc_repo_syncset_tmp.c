/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/s6rc-utils.h>
#include <s6-rc/repo.h>

static int unlink_stales_in_sub (char const *repo, size_t repolen, char const *set, char const *sub, uint32_t where, stralloc *sa, genalloc *ga, unsigned int verbosity)
{
  size_t setlen = strlen(set) ;
  size_t sublen = strlen(sub) ;
  size_t subfnlen = repolen + setlen + sublen + 10 ;
  char subfn[subfnlen + 1] ;
  memcpy(subfn, repo, repolen) ;
  memcpy(subfn + repolen, "/sources/", 9) ;
  memcpy(subfn + repolen + 9, set, setlen) ;
  subfn[repolen + 9 + setlen] = '/' ;
  memcpy(subfn + repolen + 10 + setlen, sub, sublen + 1) ;
  DIR *dir = opendir(subfn) ;
  if (!dir)
  {
    strerr_warnfu2sys("opendir ", subfn) ;
    return 0 ;
  }
  for (;;)
  {
    size_t len ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    len = strlen(d->d_name) ;
    char fn[subfnlen + len + 2] ;
    memcpy(fn, subfn, sublen) ;
    fn[sublen] = '/' ;
    memcpy(fn + sublen + 1, d->d_name, len+1) ;
    if (access(fn, F_OK) == -1)
    {
      if (errno != ENOENT)
      {
        strerr_warnfu2sys("access ", fn) ;
        goto err ;
      }
      unlink_void(fn) ;
      if (verbosity >= 3)
        strerr_warni6x("service ", d->d_name, " does not exist anymore, removed from sub ", sub, " of set ", set) ;
    }
    else
    {
      if (!genalloc_append(size_t, ga, &sa->len)) goto err ;
      if (!stralloc_catb(sa, d->d_name, len+1)) goto err ;
    }
  }
  if (errno)
  {
    strerr_warnfu2sys("readdir ", subfn) ;
    goto err ;
  }

  dir_close(dir) ;
  return 1 ;

 err:
  dir_close(dir) ;
  return 0 ;
}

int s6rc_repo_syncset_tmp (char const *repo, char const *set, stralloc *sa, genalloc *ga, unsigned int verbosity)
{
  size_t repolen = strlen(repo) ;
  size_t sabase = sa->len ;
  size_t gabase = genalloc_len(size_t, ga) ;

  for (unsigned int i = 0 ; i < 4 ; i++)
    if (unlink_stales_in_sub(repo, repolen, set, s6rc_repo_subnames[i], i, sa, ga, verbosity)) goto err ;

  {
    uint32_t n = genalloc_len(size_t, ga) ;
    char const *stillhere[n + !n] ;
    for (uint32_t i = 0 ; i < n ; i++) stillhere[i] = sa->s + genalloc_s(size_t, ga)[i] ;
    if (!s6rc_repo_fillset(repo, set, stillhere, n)) goto err ;
  }

  sa->len = sabase ;
  genalloc_setlen(size_t, ga, gabase) ;
  return 1 ;

 err:
  sa->len = sabase ;
  genalloc_setlen(size_t, ga, gabase) ;
  return 0 ;
}
