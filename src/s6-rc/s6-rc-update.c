/* ISC license. */

#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>

#define USAGE "s6-rc-update [ -l live ]"
#define dieusage() strerr_dieusage(100, USAGE)

int main (int argc, char const *const *argv)
{
  PROG = "s6-rc-update" ;
  return 0 ;
}
