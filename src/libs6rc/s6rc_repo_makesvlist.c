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

static inline int s6rc_repo_addsub (char const *sub, uint8_t i, stralloc *sa, genalloc *ga)
{
  DIR *dir = opendir(sub) ;
  if (!dir) { strerr_warnfu2sys("opendir ", sub) ; return 0 ; }
  for (;;)
  {
    s6rc_repo_sv sv = { .pos = sa->len, .sub = i } ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;

    if (!stralloc_cats(sa, d->d_name) || !stralloc_0(sa)
     || !genalloc_append(s6rc_repo_sv, ga, &sv)) goto nomem ;
  }
  dir_close(dir) ;
  if (errno) { strerr_warnfu2sys("readdir ", sub) ; return 0 ; }
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
  int sawasnull = !sa->s ;
  int gawasnull = !genalloc_s(s6rc_repo_sv, ga) ;
  char subfn[repolen + setlen + 17] ;

  memcpy(subfn, repo, repolen) ;
  memcpy(subfn + repolen, "/sources/", 9) ;
  memcpy(subfn + repolen + 9, set, setlen) ;
  subfn[repolen + 9 + setlen] = '/' ;
  for (uint8_t i = 0 ; i < 4 ; i++)
  {
    if (tab) tab[i] = genalloc_len(s6rc_repo_sv, ga) ;
    memcpy(subfn + repolen + setlen + 10, s6rc_repo_subnames[i], 7) ;
    if (!s6rc_repo_addsub(subfn, i, sa, ga)) goto err ;
  }
  return 1 ;

 err:
  if (sawasnull) stralloc_free(sa) ; else sa->len = sabase ;
  if (gawasnull) genalloc_free(s6rc_repo_sv, ga) ; else genalloc_setlen(s6rc_repo_sv, ga, gabase) ;
  return 0 ;
}
