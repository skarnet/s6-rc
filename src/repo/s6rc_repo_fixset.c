/* ISC license. */

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

static inline int s6rc_repo_fixrx (char const *repo, char const *set, uint8_t rx, s6rc_repo_sv *byname, char const **byrx, uint32_t ntot, uint32_t options, uint32_t const *rxind, uint32_t const *rxn, unsigned int verbosity, stralloc *sa, genalloc *badga, genalloc *gatmp)
{
  size_t sabase = sa->len ;
  uint32_t const *bads ;
  uint32_t badn ;
  int e = 0 ;
  if (!s6rc_repo_badrx(repo, set, byrx + rxind[rx], rxn[rx], rx, options & 1 ? 2 : 1, byname, ntot, sa, badga, gatmp)) return 111 ;
  bads = genalloc_s(uint32_t, badga) ;
  badn = genalloc_len(uint32_t, badga) ;
  if (!badn) return 0 ;
  if (options & 8) return 1 ;

  s6rc_repo_sv tomove[badn] ;
  for (uint32_t j = 0 ; j < badn ; j++)
  {
    tomove[j] = byname[bads[j]] ;
    if (verbosity >= 2)
      strerr_warni(options & 4 ? "(dry run) " : "", "in set ", set, " of repository ", repo, ": ", "moving service ", sa->s + tomove[j].pos, " from ", s6rc_repo_rxnames[tomove[j].rx], " to ", s6rc_repo_rxnames[rx]) ;
    else if (tomove[j].rx == 0 && verbosity)
      strerr_warnw(options & 4 ? "(dry run) " : "", "service ", sa->s + tomove[j].pos, " will be unmasked by an upfix to ", s6rc_repo_rxnames[rx]) ;
    else if (rx == 0 && verbosity)
      strerr_warnw(options & 4 ? "(dry run) " : "", "service ", sa->s + tomove[j].pos, " will be masked by a downfix to ", s6rc_repo_rxnames[rx]) ;
    if (tomove[j].rx == 3)
    {
      if (!(options & 2))
      {
        if (options & 4)
          strerr_warnw(options & 4 ? "(dry run) " : "", "in set ", set, " of repository ", repo, ": service ", sa->s + tomove[j].pos, " is marked as essential and cannot be downfixed (--no-force-essential)") ;
        else
        {
          strerr_warnf(options & 4 ? "(dry run) " : "", "in set ", set, " of repository ", repo, ": service ", sa->s + tomove[j].pos, " is marked as essential and cannot be downfixed (--no-force-essential)") ;
          e = 1 ;
          goto err ;
        }
      }
      else if (verbosity)
        strerr_warnw(options & 4 ? "(dry run) " : "", "service ", sa->s + tomove[j].pos, " will automatically be marked non-essential by a downfix to ", s6rc_repo_rxnames[rx]) ;
    }
  }
  if (!(options & 4))
  {
    if (!s6rc_repo_moveservices(repo, set, tomove, badn, rx, sa->s, verbosity)) { e = 111 ; goto err ; }
  }
  for (uint32_t j = 0 ; j < badn ; j++) byname[bads[j]].rx = rx ;

 err:
  genalloc_setlen(uint32_t, badga, 0) ;
  sa->len = sabase ;
  return e ;
}

int s6rc_repo_fixset (char const *repo, char const *set, uint32_t options, unsigned int verbosity, stralloc *sa, genalloc *svlist, genalloc *badga, genalloc *gatmp)
{
  int e = 0 ;
  size_t sabase = sa->len ;
  uint32_t ntot ;
  uint32_t rxind[4] ;
  uint32_t rxlen[4] ;
  s6rc_repo_sv *byname ;
  genalloc_setlen(s6rc_repo_sv, svlist, 0) ;
  if (!s6rc_repo_makesvlist(repo, set, sa, svlist, rxind)) return 0 ;
  byname = genalloc_s(s6rc_repo_sv, svlist) ;
  ntot = genalloc_len(s6rc_repo_sv, svlist) ;
  if (!ntot) return 0 ;
  for (uint8_t rx = 0 ; rx < 4 ; rx++) rxlen[rx] = (rx == 3 ? ntot : rxind[rx+1]) - rxind[rx] ;

  char const *byrx[ntot] ;
  char byrx_storage[sa->len - sabase] ;
  memcpy(byrx_storage, sa->s + sabase, sa->len - sabase) ;
  for (uint32_t i = 0 ; i < ntot ; i++) byrx[i] = byrx_storage + byname[i].pos - sabase ;
  qsortr(byname, ntot, sizeof(s6rc_repo_sv), &s6rc_repo_sv_cmpr, sa->s) ;

  for (uint8_t rx = 0 ; rx < 4 ; rx++) if (rxlen[rx])
  {
    e = s6rc_repo_fixrx(repo, set, rx, byname, byrx, ntot, options, rxind, rxlen, verbosity, sa, badga, gatmp) ;
    if (e) break ;
  }

  genalloc_setlen(s6rc_repo_sv, svlist, 0) ;
  return e ;
}
