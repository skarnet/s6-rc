/* ISC license. */

#include <skalibs/posixplz.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

int s6rc_repo_makesvlist_byname (char const *repo, char const *set, stralloc *sa, genalloc *ga)
{
  if (!s6rc_repo_makesvlist(repo, set, sa, ga, 0)) return 0 ;
  qsortr(genalloc_s(s6rc_repo_sv, ga), genalloc_len(s6rc_repo_sv, ga), sizeof(s6rc_repo_sv), &s6rc_repo_sv_cmpr, sa->s) ;
  return 1 ;
}
