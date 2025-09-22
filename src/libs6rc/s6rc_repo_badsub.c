/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/direntry.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

static int strqcmp (void const *a, void const *b)
{
  return strcmp(*(char const *const *)a, *(char const *const *)b) ;
}

int s6rc_repo_badsub (char const *repo, char const *set, char const **services, uint32_t n, uint8_t newsub, uint8_t what, s6rc_repo_sv const *svlist, uint32_t ntot, stralloc *sa, genalloc *badga)
{
  int sawasnull = !!sa->s ;
  int gawasnull = !!genalloc_s(uint32_t, badga) ;
  size_t sabase = sa->len ;
  size_t gabase = genalloc_len(uint32_t, badga) ;
  genalloc fulldeps = GENALLOC_ZERO ;
  size_t fulln ;
  size_t const *ind ;
  uint32_t mid ;

  if (newsub < 3 && (what & 1) && s6rc_repo_listalldeps(repo, services, n, sa, &fulldeps, 0)) return 0 ;
  mid = genalloc_len(size_t, &fulldeps) ;
  if (newsub > 0 && (what & 2) && s6rc_repo_listalldeps(repo, services, n, sa, &fulldeps, 1)) goto err ;

  qsort(services, n, sizeof(char const *), &strqcmp) ;
  fulln = genalloc_len(size_t, &fulldeps) ;
  ind = genalloc_s(size_t, &fulldeps) ;

  for (uint32_t i = 0 ; i < fulln ; i++)
  {
    s6rc_repo_sv *p ;
    char const *cur = sa->s + ind[i] ;
    if (bsearch(&cur, services, n, sizeof(char const *), &strqcmp)) continue ;
    p = bsearchr(cur, svlist, ntot, sizeof(s6rc_repo_sv), &s6rc_repo_sv_bcmpr, sa->s) ;
    if (!p)
    {
      strerr_warnfu6x("find service ", cur, " in set ", set, " of repository ", repo) ;
      goto err ;
    }
    if (i >= mid ? p->sub < newsub : p->sub > newsub)
    {
      uint32_t k = p - svlist ;
      if (!genalloc_append(uint32_t, badga, &k))
      {
        strerr_warnfu1sys("make bad sub list") ;
        goto err ;
      }
    }
  }
  genalloc_free(size_t, &fulldeps) ;
  return 1 ;

 err:
  genalloc_free(size_t, &fulldeps) ;
  if (sawasnull) stralloc_free(sa) ; else sa->len = sabase ;
  if (gawasnull) genalloc_free(uint32_t, badga) ; else genalloc_setlen(uint32_t, badga, gabase) ;
  return 0 ;
}
