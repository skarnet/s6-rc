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

#define dienomem() strerr_diefu1sys(111, "stralloc_catb")

typedef int qsortcmp_func (void const *, void const *) ;
typedef qsortcmp_func *qsortcmp_func_ref ;

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

int s6rc_repo_listinconsistent (char const *repo, char const *set, char const **services, size_t n, uint8_t newsub, s6rc_repo_sv const *svlist, size_t nlist, stralloc *sa, genalloc *inconga)
{
  size_t sabase = sa->len ;
  genalloc fulldeps = GENALLOC_ZERO ;
  size_t fulln ;
  size_t const *ind ;

  if (s6rc_repo_listalldeps(repo, services, n, sa, &fulldeps, sub >= 2) <= 0) return 0 ;
  qsort(services, n, sizeof(char const *), &strqcmp) ;
  fulln = genalloc_len(size_t, &fulldeps) ;
  ind = genalloc_s(size_t, &fulldeps) ;

  for (size_t i = 0 ; i < fulln ; i++)
  {
    s6rc_repo_sv *p ;
    char const *cur = sa->s + ind[i] ;
    uint8_t oldsub ;
    if (bsearch(&cur, services, n, sizeof(char const *), &strqcmp)) continue ;
    p = bsearchr(cur, svlist, nlist, sizeof(s6rc_repo_sv), &s6rc_repo_sv_bcmpr, sa) ;
    if (!p)
    {
    }
  }
}
