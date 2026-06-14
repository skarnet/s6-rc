/* ISC license. */

#include <string.h>
#include <unistd.h>

#include <skalibs/types.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/tai.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-format-upgrade [ -v verbosity ] [ -t timeout ] [ -l live ] [ -b ] newdb"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_BLOCK = 0x01,
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_LIVEDIR,
  GOLA_TIMEOUT,
  GOLA_N
} ;

static unsigned int verbosity = 1 ;


 /* This function will change if format changes become heavier */

static inline void update_livedir (char const *live, char const *newcompiled, tain const *deadline)
{
  size_t livelen = strlen(live) ;
  char cfn[livelen + 10] ;
  memcpy(cfn, live, livelen) ;
  memcpy(cfn + livelen, "/compiled", 10) ;
  if (!atomic_symlink4(newcompiled, cfn, 0, 0))
    strerr_diefusys(111, "atomic_symlink4 ", cfn, " to ", newcompiled) ;
}

int main (int argc, char const *const *argv, char const *const *envp)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'b', .lo = "block", .clear = 0, .set = GOLB_BLOCK },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'l', .lo = "livedir", .i = GOLA_LIVEDIR },
    { .so = 't', .lo = "timeout", .i = GOLA_TIMEOUT },
  } ;
  uint64_t wgolb = 0 ;
  char const *wgola[GOLA_N] = { 0 } ;
  tain deadline = TAIN_INFINITE_RELATIVE ;
  int livelock ;
  wgola[GOLA_LIVEDIR] = S6RC_LIVEDIR ;
  PROG = "s6-rc-format-upgrade" ;

  {
    unsigned int golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
    argc -= golc ; argv += golc ;
  }

  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief(100, "verbosity", " must be an unsigned integer") ;
  if (wgola[GOLA_TIMEOUT])
  {
    unsigned int t ;
    if (!uint0_scan(wgola[GOLA_TIMEOUT], &t))
      strerr_dief(100, "timeout", " must be an unsigned integer") ;
    if (t) tain_from_millisecs(&deadline, t) ;
  }

  if (!argc) dieusage() ;
  if (argv[0][0] != '/')
    strerr_dief(100, argv[0], " is not an absolute path") ;

  tain_now_set_stopwatch_g() ;
  tain_add_g(&deadline, &deadline) ;

  if (!s6rc_lock(wgola[GOLA_LIVEDIR], 2, &livelock, 0, 0, 0, wgolb & GOLB_BLOCK))
    strerr_diefusys(111, "take lock on ", wgola[GOLA_LIVEDIR]) ;

  update_livedir(wgola[GOLA_LIVEDIR], argv[0], &deadline) ;
  _exit(0) ;
}
