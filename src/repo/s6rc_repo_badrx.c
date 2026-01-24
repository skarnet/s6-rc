/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/bytestr.h>
#include <skalibs/direntry.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

int s6rc_repo_badrx (char const *repo, char const *set, char const **services, uint32_t n, uint8_t newrx, uint8_t what, s6rc_repo_sv const *svlist, uint32_t ntot, stralloc *sa, genalloc *badga, genalloc *fulldeps)
{
  size_t sabase = sa->len ;
  size_t gabase = genalloc_len(uint32_t, badga) ;
  size_t fulln ;
  size_t const *ind ;
  uint32_t mid ;

  if (newrx < 3 && (what & 1) && s6rc_repo_listalldeps(repo, services, n, sa, fulldeps, 0)) return 0 ;
  mid = genalloc_len(size_t, fulldeps) ;
  if (newrx > 0 && (what & 2) && s6rc_repo_listalldeps(repo, services, n, sa, fulldeps, 1)) goto err ;

  qsort(services, n, sizeof(char const *), &str_cmp) ;
  fulln = genalloc_len(size_t, fulldeps) ;
  ind = genalloc_s(size_t, fulldeps) ;

  for (uint32_t i = 0 ; i < fulln ; i++)
  {
    s6rc_repo_sv *p ;
    char const *cur = sa->s + ind[i] ;
    if (bsearch(&cur, services, n, sizeof(char const *), &str_cmp)) continue ;
    p = bsearchr(cur, svlist, ntot, sizeof(s6rc_repo_sv), &s6rc_repo_sv_bcmpr, sa->s) ;
    if (!p)
    {
      strerr_warnfu6x("find service ", cur, " in set ", set, " of repository ", repo) ;
      goto err ;
    }
    if (i >= mid ? p->rx < newrx : p->rx > newrx)
    {
      uint32_t k = p - svlist ;
      if (!genalloc_append(uint32_t, badga, &k))
      {
        strerr_warnfu1sys("make bad rx list") ;
        goto err ;
      }
    }
  }

  if ((!newrx && (what & 1)) || (newrx && (what & 2)))
  {
    for (uint32_t i = 0 ; i < fulln ; i++)
    {
      int e = s6rc_repo_badpipeline(repo, set, ind[i], svlist, ntot, newrx, sa, badga) ;
      if (e) goto err ;
    }
  }
  genalloc_setlen(size_t, fulldeps, 0) ;
  return 1 ;

 err:
  genalloc_setlen(size_t, fulldeps, 0) ;
  sa->len = sabase ;
  genalloc_setlen(uint32_t, badga, gabase) ;
  return 0 ;
}
