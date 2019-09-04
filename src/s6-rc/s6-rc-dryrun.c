/* ISC license. */

#include <skalibs/types.h>
#include <skalibs/buffer.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/tai.h>
#include <skalibs/iopause.h>

#define USAGE "s6-rc-dryrun [ -v verbosity ] [ -t timeout ] args..."
#define dieusage() strerr_dieusage(100, USAGE)

int main (int argc, char const *const *argv)
{
  unsigned int verbosity = 1 ;
  tain_t deadline ;
  PROG = "s6-rc-dryrun" ;
  {
    unsigned int t = 1000 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "v:t:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v': if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 't': if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    tain_from_millisecs(&deadline, t) ;
  }
  if (!argc) dieusage() ;
  if (verbosity)
  {
    buffer_puts(buffer_1, PROG) ;
    buffer_put(buffer_1, ":", 1) ;
    for (; *argv ; argv++)
    {
      buffer_put(buffer_1, " ", 1) ;
      buffer_puts(buffer_1, *argv) ;
    }
    buffer_putflush(buffer_1, "\n", 1) ;
  }
  tain_now_set_stopwatch() ;
  tain_now_g() ;
  tain_add_g(&deadline, &deadline) ;
  deepsleepuntil_g(&deadline) ;
  return 0 ;
}
