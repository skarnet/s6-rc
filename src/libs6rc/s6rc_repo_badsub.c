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

static int s6rc_repo_sv_bcmpr (void const *a, void const *b, void *aux)
{
  char const *key = a ;
  s6rc_repo_sv const *elem = b ;
  stralloc *sa = aux ;
  return strcmp(key, sa->s + elem->pos) ;
}

static int strqcmp (void const *a, void const *b)
{
  char const *const *aa = a ;
  char const *const *bb = b ;
  return strcmp(*aa, *bb) ;
}

int s6rc_repo_badsub (char const *repo, char const *set, char const **services, size_t n, uint8_t newsub, s6rc_repo_sv const *svlist, size_t nlist, stralloc *sa, genalloc *badga)
{
  int sawasnull = !!sa->s ;
  size_t sabase = sa->len ;
  int gawasnull = !!genalloc_s(uint32_t, badga) ;
  size_t gabase = genalloc_len(uint32_t, badga) ;
  genalloc fulldeps = GENALLOC_ZERO ;
  size_t fulln ;
  size_t const *ind ;

  if (s6rc_repo_listalldeps(repo, services, n, sa, &fulldeps, newsub >= 2) <= 0) return 0 ;
  qsort(services, n, sizeof(char const *), &strqcmp) ;
  fulln = genalloc_len(size_t, &fulldeps) ;
  ind = genalloc_s(size_t, &fulldeps) ;

  for (size_t i = 0 ; i < fulln ; i++)
  {
    s6rc_repo_sv *p ;
    char const *cur = sa->s + ind[i] ;
    if (bsearch(&cur, services, n, sizeof(char const *), &strqcmp)) continue ;
    p = bsearchr(cur, svlist, nlist, sizeof(s6rc_repo_sv), &s6rc_repo_sv_bcmpr, sa) ;
    if (!p)
    {
      strerr_warnfu6x("find service ", cur, " in set ", set, " of repository ", repo) ;
      goto err ;
    }
    if (newsub >= 2 ? p->sub < newsub : p->sub > newsub)
    {
      uint32_t n = p - svlist ;
      if (!genalloc_append(uint32_t, badga, &n))
      {
        strerr_warnfu1sys("make bad sub list") ;
        goto err ;
      }
    }
  }
  genalloc_free(size_t, &fulldeps) ;
  return 1 ;

 err:
  if (sawasnull) stralloc_free(sa) ; else sa->len = sabase ;
  if (gawasnull) genalloc_free(uint32_t, badga) ; else genalloc_setlen(uint32_t, badga, gabase) ;
  return 0 ;
}
