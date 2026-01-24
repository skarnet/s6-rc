/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/bytestr.h>
#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

static int unlink_stales_in_sub (char const *repo, size_t repolen, char const *set, uint8_t sub, stralloc *sa, genalloc *ga, unsigned int verbosity)
{
  size_t setlen = strlen(set) ;
  size_t subfnlen = repolen + setlen + 16 ;
  char subfn[subfnlen + 1] ;
  memcpy(subfn, repo, repolen) ;
  memcpy(subfn + repolen, "/sources/", 9) ;
  memcpy(subfn + repolen + 9, set, setlen) ;
  subfn[repolen + 9 + setlen] = '/' ;
  memcpy(subfn + repolen + 10 + setlen, s6rc_repo_subnames[sub], 7) ;
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
    memcpy(fn, subfn, subfnlen) ;
    fn[subfnlen] = '/' ;
    memcpy(fn + subfnlen + 1, d->d_name, len+1) ;
    if (access(fn, F_OK) == -1)
    {
      if (errno != ENOENT)
      {
        strerr_warnfu2sys("access ", fn) ;
        goto err ;
      }
      unlink_void(fn) ;
      if (verbosity >= 3)
        strerr_warni6x("service ", d->d_name, " does not exist anymore, removed from sub ", s6rc_repo_subnames[sub], " of set ", set) ;
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

  for (uint8_t sub = 0 ; sub < 4 ; sub++)
    if (!unlink_stales_in_sub(repo, repolen, set, sub, sa, ga, verbosity)) goto err ;

  {
    uint32_t n = genalloc_len(size_t, ga) ;
    char const *stillhere[n + !n] ;
    for (uint32_t i = 0 ; i < n ; i++) stillhere[i] = sa->s + genalloc_s(size_t, ga)[i] ;
    qsort(stillhere, n, sizeof(char const *), &str_cmp) ;
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
