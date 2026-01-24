/* ISC license. */

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/direntry.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

static inline int s6rc_repo_addrx (char const *rx, uint8_t i, stralloc *sa, genalloc *ga)
{
  DIR *dir = opendir(rx) ;
  if (!dir) { strerr_warnfu2sys("opendir ", rx) ; return 0 ; }
  for (;;)
  {
    s6rc_repo_sv sv = { .pos = sa->len, .rx = i } ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;

    if (!stralloc_cats(sa, d->d_name) || !stralloc_0(sa)
     || !genalloc_append(s6rc_repo_sv, ga, &sv)) goto nomem ;
  }
  dir_close(dir) ;
  if (errno) { strerr_warnfu2sys("readdir ", rx) ; return 0 ; }
  return 1 ;

 nomem:
  strerr_warnfu1sys("stralloc_catb") ;
  return 0 ;
}

int s6rc_repo_makesvlist (char const *repo, char const *set, stralloc *sa, genalloc *ga, uint32_t *tab)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t sabase = sa->len ;
  size_t gabase = genalloc_len(s6rc_repo_sv, ga) ;
  char rxfn[repolen + setlen + 17] ;

  memcpy(rxfn, repo, repolen) ;
  memcpy(rxfn + repolen, "/sources/", 9) ;
  memcpy(rxfn + repolen + 9, set, setlen) ;
  rxfn[repolen + 9 + setlen] = '/' ;
  for (uint8_t i = 0 ; i < 4 ; i++)
  {
    if (tab) tab[i] = genalloc_len(s6rc_repo_sv, ga) ;
    memcpy(rxfn + repolen + setlen + 10, s6rc_repo_rxnames[i], 7) ;
    if (!s6rc_repo_addrx(rxfn, i, sa, ga)) goto err ;
  }
  return 1 ;

 err:
  sa->len = sabase ;
  genalloc_setlen(s6rc_repo_sv, ga, gabase) ;
  return 0 ;
}
