/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/uint64.h>
#include <skalibs/posixplz.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-change [ -v verbosity ] [ -r repo ] [ -E | -e ] [ -f | -I fail|pull|warn ] [ -n ] set newrx services..."
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_FORCE_ESSENTIAL = 0x01,
  GOLB_IGNORE_DEPENDENCIES = 0x02,
  GOLB_DRYRUN = 0x04
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_FORCELEVEL,
  GOLA_N
} ;

struct rxname_s
{
  char const *name ;
  uint8_t rx ;
} ;

static int rxname_cmp (void const *a, void const *b)
{
  return strcmp((char const *)a, ((struct rxname_s const *)b)->name) ;
}

int main (int argc, char const *const *argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'E', .lo = "no-force-essential", .clear = GOLB_FORCE_ESSENTIAL, .set = 0 },
    { .so = 'e', .lo = "force-essential", .clear = 0, .set = GOLB_FORCE_ESSENTIAL },
    { .so = 'f', .lo = "ignore-dependencies", .clear = 0, .set = GOLB_IGNORE_DEPENDENCIES },
    { .so = 'n', .lo = "dry-run", .clear = 0, .set = GOLB_DRYRUN }
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR },
    { .so = 'I', .lo = "if-dependencies-found", .i = GOLA_FORCELEVEL }
  } ;
  static struct rxname_s const accepted_forcelevels[] =
  {
    { .name = "fail", .rx = 0 },
    { .name = "pull", .rx = 2 },
    { .name = "warn", .rx = 1 }
  } ;
  static struct rxname_s const accepted_rxs[] =
  {
    { .name = "activate", .rx = 2 },
    { .name = "active", .rx = 2 },
    { .name = "always", .rx = 3 },
    { .name = "deactivate", .rx = 1 },
    { .name = "disable", .rx = 1 },
    { .name = "disabled", .rx = 1 },
    { .name = "enable", .rx = 2 },
    { .name = "enabled", .rx = 2 },
    { .name = "essential", .rx = 3 },
    { .name = "inactive", .rx = 1 },
    { .name = "latent", .rx = 1 },
    { .name = "make-essential", .rx = 3 },
    { .name = "mask", .rx = 0 },
    { .name = "masked", .rx = 0 },
    { .name = "onboot", .rx = 2 },
    { .name = "suppress", .rx = 0 },
    { .name = "unmask", .rx = 1 },
    { .name = "unmasked", .rx = 1 },
    { .name = "usable", .rx = 1 }
  } ;
  stralloc storage = STRALLOC_ZERO ;
  genalloc svlist = GENALLOC_ZERO ;  /* s6rc_repo_sv */
  genalloc indices = GENALLOC_ZERO ;  /* size_t then uint32_t */
  genalloc gatmp = GENALLOC_ZERO ;  /* size_t whatever */
  int fdlock ;
  unsigned int verbosity = 1 ;
  unsigned int forcelevel = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;
  struct rxname_s *newrx ;
  size_t max = 0, sabase ;
  s6rc_repo_sv *list ;
  uint32_t listn, n ;

  PROG = "s6-rc-set-change" ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (wgola[GOLA_FORCELEVEL])
  {
    struct rxname_s *p = bsearch(wgola[GOLA_FORCELEVEL], accepted_forcelevels, sizeof(accepted_forcelevels)/sizeof(struct rxname_s), sizeof(struct rxname_s), &rxname_cmp) ;
    if (!p) strerr_dief1x(100, "if-dependencies-found needs to be fail, warn or pull") ;
    forcelevel = p->rx ;
  }
  if (argc < 3) dieusage() ;
  s6rc_repo_sanitize_setname(argv[0]) ;
  newrx = bsearch(argv[1], accepted_rxs, sizeof(accepted_rxs)/sizeof(struct rxname_s), sizeof(struct rxname_s), &rxname_cmp) ;
  if (!newrx) strerr_dief2x(100, "unrecognized state change directive: ", argv[1]) ;
  if (newrx->rx == 3 && !(wgolb & GOLB_FORCE_ESSENTIAL))
    strerr_diefu1x(100, "artificially mark a service as essential without --force-essential") ;
  for (unsigned int i = 2 ; i < argc ; i++) s6rc_repo_sanitize_svname(argv[i]) ;

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;
  if (!s6rc_repo_makesvlist_byname(wgola[GOLA_REPODIR], argv[0], &storage, &svlist)) _exit(111) ;
  list = genalloc_s(s6rc_repo_sv, &svlist) ;
  listn = genalloc_len(s6rc_repo_sv, &svlist) ;
  sabase = storage.len ;
  {
    int e = s6rc_repo_flattenservices(wgola[GOLA_REPODIR], argv + 2, argc - 2, &storage, &indices) ;
    if (e) _exit(e) ;
  }
  n = genalloc_len(size_t, &indices) ;
  if (!n) strerr_dief1x(101, "can't happen: 0 services in flattened list!") ;

  s6rc_repo_sv starting[n] ;
  uint32_t ind[n] ;

  for (uint32_t i = 0 ; i < n ; i++)
  {
    char const *s = storage.s + genalloc_s(size_t, &indices)[i] ;
    s6rc_repo_sv *p = bsearchr(s, list, listn, sizeof(s6rc_repo_sv), &s6rc_repo_sv_bcmpr, storage.s) ;
    if (!p) strerr_dief7x(102, "inconsistent view in set ", argv[0], " of repository ", wgola[GOLA_REPODIR], ": service ", s, " is defined in the reference database but not in the textual representation of the set") ;
    starting[i] = *p ;
    ind[i] = p - list ;
    max += strlen(s) + 1 ;
  }

  storage.len = sabase ;
  genalloc_setlen(size_t, &indices, 0) ;

  if (!(wgolb & GOLB_IGNORE_DEPENDENCIES))
  {
    size_t m = 0 ;
    char const *tmpstart[n] ;
    char tmpstore[max] ;

    for (uint32_t i = 0 ; i < n ; i++)
    {
      size_t len ;
      list[ind[i]].rx = newrx->rx ;
      tmpstart[i] = tmpstore + m ;
      len = strlen(storage.s + list[ind[i]].pos) + 1 ;
      memcpy(tmpstore + m, storage.s + list[ind[i]].pos, len) ;
      m += len ;
    }
    if (!s6rc_repo_badrx(wgola[GOLA_REPODIR], argv[0], tmpstart, n, newrx->rx, 3, list, listn, &storage, &indices, &gatmp)) _exit(111) ;
    // genalloc_free(size_t, &gatmp) ;
    if (genalloc_len(uint32_t, &indices))
    {
      uint32_t const *bads = genalloc_s(uint32_t, &indices) ;
      uint32_t badn = genalloc_len(uint32_t, &indices) ;
      if (verbosity || !forcelevel)
      {
        char const *arg[10 + (badn << 1)] ;
        arg[0] = PROG ;
        arg[1] = ": " ;
        arg[2] = !forcelevel ? "fatal" : "warning" ;
        arg[3] = ": the following services (" ;
        arg[4] = newrx->rx >= 2 ? "dependencies of" : "depending on" ;
        arg[5] = " the ones given as arguments, or part of the same pipeline) " ;
        arg[6] = forcelevel == 2 ? "are also being" : "also need to be" ;
        arg[7] = " changed to \"" ;
        arg[8] = s6rc_repo_rxnames[newrx->rx] ;
        arg[9] = "\": " ;
        for (uint32_t i = 0 ; i < badn ; i++)
        {
          arg[10 + (i << 1)] = storage.s + list[bads[i]].pos ;
          arg[11 + (i << 1)] = i < badn ? " " : "" ;
        }
        strerr_warnv(arg, 10 + (badn << 1)) ;
      }
      if (!forcelevel) _exit(1) ;
      if (wgolb & GOLB_DRYRUN) _exit(0) ;
      if (forcelevel == 2)
      {
        s6rc_repo_sv full[n + badn] ;
        for (uint32_t i = 0 ; i < n ; i++) full[i] = starting[i] ;
        for (uint32_t i = 0 ; i < badn ; i++) full[n + i] = list[bads[i]] ;
        if (!s6rc_repo_moveservices(wgola[GOLA_REPODIR], argv[0], full, n + badn, newrx->rx, storage.s, verbosity)) _exit(111) ;
        _exit(0) ;
      }
    }
  }
  // genalloc_free(uint32_t, &indices) ;

  if (!(wgolb & GOLB_DRYRUN))
  {
    if (!s6rc_repo_moveservices(wgola[GOLA_REPODIR], argv[0], starting, n, newrx->rx, storage.s, verbosity)) _exit(111) ;
  }
  _exit(0) ;
}
