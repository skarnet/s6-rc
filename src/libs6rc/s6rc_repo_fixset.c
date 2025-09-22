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

static inline int s6rc_repo_fixsub (char const *repo, char const *set, uint8_t sub, s6rc_repo_sv *byname, char const **bysub, uint32_t ntot, uint32_t options, uint32_t const *subind, uint32_t const *subn, unsigned int verbosity, stralloc *sa, genalloc *badga)
{
  size_t sabase = sa->len ;
  uint32_t gabase = genalloc_len(uint32_t, badga) ;
  uint32_t const *bads ;
  uint32_t badn ;
  if (!s6rc_repo_badsub(repo, set, bysub + subind[sub], subn[sub], sub, options & 1 ? 2 : 1, byname, ntot, sa, badga)) return 0 ;
  bads = genalloc_s(uint32_t, badga) ;
  badn = genalloc_len(uint32_t, badga) ;
  if (!badn) return 1 ;

  s6rc_repo_sv tomove[badn] ;
  for (uint32_t j = 0 ; j < badn ; j++)
  {
    tomove[j] = byname[bads[j]] ;
    if (verbosity >= 2)
      strerr_warni("in set ", set, " of repository ", repo, ": ", options & 1 ? "up" : "down", "fixing service ", sa->s + tomove[j].pos, " from ", s6rc_repo_subnames[tomove[j].sub], " to ", s6rc_repo_subnames[sub]) ;
    if (tomove[j].sub == 0 && verbosity)
      strerr_warnw("service ", sa->s + tomove[j].pos, " is being automatically unmasked by an upfix to ", s6rc_repo_subnames[sub]) ;
    if (tomove[j].sub == 3)
    {
      if (!(options & 2))
      {
        strerr_warnf("in set ", set, " of repository ", repo, ": service ", sa->s + tomove[j].pos, " is marked as essential and cannot be downfixed. If you are sure of yourself, try --force-essential") ;
        goto err ;
      }
      if (verbosity)
      strerr_warnw("service ", sa->s + tomove[j].pos, " is being automatically marked non-essential by a downfix to ", s6rc_repo_subnames[sub]) ;
    }
  }
  if (!s6rc_repo_moveservices(repo, set, tomove, badn, sub, sa->s, verbosity)) goto err ;
  for (uint32_t j = 0 ; j < badn ; j++) byname[bads[j]].sub = sub ;
  genalloc_setlen(uint32_t, badga, gabase) ;
  sa->len = sabase ;
  return 1 ;

 err:
  genalloc_setlen(uint32_t, badga, gabase) ;
  sa->len = sabase ;
  return 0 ;
}

int s6rc_repo_fixset (char const *repo, char const *set, uint32_t options, unsigned int verbosity, stralloc *sa, genalloc *svlist, genalloc *badga)
{
  int sawasnull = !sa->s ;
  int svwasnull = !genalloc_s(s6rc_repo_sv, svlist) ;
  int gawasnull = !genalloc_s(uint32_t, badga) ;
  size_t sabase = sa->len ;
  uint32_t svbase = genalloc_len(s6rc_repo_sv, svlist) ;
  uint32_t gabase = genalloc_len(uint32_t, badga) ;
  uint32_t ntot ;
  uint32_t subind[4] ;
  uint32_t sublen[4] ;
  s6rc_repo_sv *byname ;
  if (!s6rc_repo_makesvlist(repo, set, sa, svlist, subind)) return 0 ;
  byname = genalloc_s(s6rc_repo_sv, svlist) ;
  ntot = genalloc_len(s6rc_repo_sv, svlist) ;
  if (!ntot) return 1 ;
  for (uint8_t i = 0 ; i < 4 ; i++) sublen[i] = (i == 3 ? ntot : subind[i+1]) - subind[i] ;

  char const *bysub[ntot] ;
  char bysub_storage[sa->len - sabase] ;
  memcpy(bysub_storage, sa->s + sabase, sa->len - sabase) ;
  for (uint32_t i = 0 ; i < ntot ; i++) bysub[i] = bysub_storage + byname[i].pos - sabase ;
  qsortr(byname, ntot, sizeof(s6rc_repo_sv), &s6rc_repo_sv_cmpr, sa->s) ;

  for (uint8_t i = 0 ; i < 4 ; i++) if (sublen[i])
  {
    if (!s6rc_repo_fixsub(repo, set, i, byname, bysub, ntot, options, subind, sublen, verbosity, sa, badga))
      goto err ;
  }

  genalloc_setlen(uint32_t, badga, gabase) ;
  genalloc_setlen(s6rc_repo_sv, svlist, svbase) ;
  sa->len = sabase ;
  return 1 ;

 err:
  if (gawasnull) genalloc_free(uint32_t, badga) ; else genalloc_setlen(uint32_t, badga, gabase) ;
  if (svwasnull) genalloc_free(s6rc_repo_sv, svlist) ; else genalloc_setlen(s6rc_repo_sv, svlist, svbase) ;
  if (sawasnull) stralloc_free(sa) ; else sa->len = sabase ;
  return 0 ;
}
