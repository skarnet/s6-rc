/* ISC license. */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <skalibs/strerr2.h>
#include <skalibs/sig.h>
#include <skalibs/selfpipe.h>
#include <skalibs/tai.h>

#include "clientrules.h"
#include "transition.h"
#include "main.h"
#include "signals.h"

static inline void activate_lameduck (void)
{
  if (!flags.lameduck)
  {
    flags.lameduck = 1 ;
    tain_add_g(&lameduckdeadline, &lameduckdeadline) ;
  }
}

static inline void reload_config (void)
{
  clientrules_reload() ;
}

static inline void wait_children (void)
{
  for (;;)
  {
    unsigned int j = 0 ;
    int wstat ;
    pid_t r = wait_nohang(&wstat) ;
    if (r < 0)
      if (errno == ECHILD) break ;
      else strerr_diefu1sys(111, "wait for children") ;
    else if (!r) break ;
    for (; j < ntransitions ; j++) if (transitions[j].pid == r) break ;
    if (r < ntransitions)
    {
      transition_t tr = transitions[j] ;
      transitions[j] = transitions[--ntransitions] ;
      if (!WIFSIGNALED(wstat) && !WEXITSTATUS(wstat))
        transition_success(&tr) ;
      else
        transition_failure(&tr, !!WIFSIGNALED(wstat), WIFSIGNALED(wstat) ? WTERMSIG(wstat) : WEXITSTATUS(wstat)) ;
    }
  }
}

void signals_handle ()
{
  for (;;)
  {
    switch (selfpipe_read())
    {
      case -1 : strerr_diefu1sys(111, "selfpipe_read") ;
      case 0 : return ;
      case SIGTERM :
        activate_lameduck() ;
        break ;
      case SIGHUP :
        reload_config() ;
        break ;
      case SIGCHLD :
        wait_children() ;
        break ;
    }
  }
}

int signals_init ()
{
  int fd = selfpipe_init() ;
  if (fd < 0) strerr_diefu1sys(111, "selfpipe_init") ;
  if (sig_ignore(SIGPIPE) < 0) strerr_diefu1sys(111, "ignore SIGPIPE") ;
  {
    sigset_t set ;
    sigemptyset(&set) ;
    sigaddset(&set, SIGTERM) ;
    sigaddset(&set, SIGHUP) ;
    if (selfpipe_trapset(&set) < 0) strerr_diefu1sys(111, "trap signals") ;
  }
  return fd ;
}
