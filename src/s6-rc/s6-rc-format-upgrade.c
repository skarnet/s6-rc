/* ISC license. */

#include <string.h>

#include <skalibs/types.h>
#include <skalibs/strerr.h>
#include <skalibs/sgetopt.h>
#include <skalibs/tai.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-format-upgrade [ -v verbosity ] [ -t timeout ] [ -l live ] [ -b ] newdb"
#define dieusage() strerr_dieusage(100, USAGE)

static unsigned int verbosity = 1 ;


 /* This function will change if format changes become heavier */

static inline void update_livedir (char const *live, char const *newcompiled, tain const *deadline)
{
  size_t livelen = strlen(live) ;
  char cfn[livelen + 10] ;
  memcpy(cfn, live, livelen) ;
  memcpy(cfn + livelen, "/compiled", 10) ;
  if (!atomic_symlink(newcompiled, cfn, PROG))
    strerr_diefu4sys(111, "atomic_symlink ", cfn, " to ", newcompiled) ;
}


int main (int argc, char const *const *argv, char const *const *envp)
{
  tain deadline ;
  char const *live = S6RC_LIVE_BASE ;
  int blocking = 0 ;
  int livelock ;
  PROG = "s6-rc-format-upgrade" ;
  {
    unsigned int t = 0 ;
    subgetopt l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "v:t:l:b", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 't' : if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        case 'l' : live = l.arg ; break ;
        case 'b' : blocking = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&deadline, t) ;
    else deadline = tain_infinite_relative ;
  }
  if (!argc) dieusage() ;
  if (argv[0][0] != '/')
    strerr_dief2x(100, argv[0], " is not an absolute path") ;

  tain_now_set_stopwatch_g() ;
  tain_add_g(&deadline, &deadline) ;

  if (!s6rc_lock(live, 2, &livelock, 0, 0, 0, blocking))
    strerr_diefu2sys(111, "take lock on ", live) ;

  update_livedir(live, argv[0], &deadline) ;
  return 0 ;
}
