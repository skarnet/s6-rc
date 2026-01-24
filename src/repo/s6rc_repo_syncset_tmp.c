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

static int unlink_stales_in_rx (char const *repo, size_t repolen, char const *set, uint8_t rx, stralloc *sa, genalloc *ga, unsigned int verbosity)
{
  size_t setlen = strlen(set) ;
  size_t rxfnlen = repolen + setlen + 16 ;
  char rxfn[rxfnlen + 1] ;
  memcpy(rxfn, repo, repolen) ;
  memcpy(rxfn + repolen, "/sources/", 9) ;
  memcpy(rxfn + repolen + 9, set, setlen) ;
  rxfn[repolen + 9 + setlen] = '/' ;
  memcpy(rxfn + repolen + 10 + setlen, s6rc_repo_rxnames[rx], 7) ;
  DIR *dir = opendir(rxfn) ;
  if (!dir)
  {
    strerr_warnfu2sys("opendir ", rxfn) ;
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
    char fn[rxfnlen + len + 2] ;
    memcpy(fn, rxfn, rxfnlen) ;
    fn[rxfnlen] = '/' ;
    memcpy(fn + rxfnlen + 1, d->d_name, len+1) ;
    if (access(fn, F_OK) == -1)
    {
      if (errno != ENOENT)
      {
        strerr_warnfu2sys("access ", fn) ;
        goto err ;
      }
      unlink_void(fn) ;
      if (verbosity >= 3)
        strerr_warni6x("service ", d->d_name, " does not exist anymore, removed from rx ", s6rc_repo_rxnames[rx], " of set ", set) ;
    }
    else
    {
      if (!genalloc_append(size_t, ga, &sa->len)) goto err ;
      if (!stralloc_catb(sa, d->d_name, len+1)) goto err ;
    }
  }
  if (errno)
  {
    strerr_warnfu2sys("readdir ", rxfn) ;
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

  for (uint8_t rx = 0 ; rx < 4 ; rx++)
    if (!unlink_stales_in_rx(repo, repolen, set, rx, sa, ga, verbosity)) goto err ;

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
