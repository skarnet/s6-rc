/* ISC license. */

#include <unistd.h>
#include <errno.h>
#include <skalibs/uint.h>
#include <skalibs/bytestr.h>
#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/tai.h>
#include <s6/s6-fdholder.h>

#define USAGE "s6-rc-fdholder-filler [ -1 ] [ -t timeout ] longrunnames..."
#define dieusage() strerr_dieusage(100, USAGE)

int main (int argc, char const *const *argv)
{
  s6_fdholder_t a = S6_FDHOLDER_ZERO ;
  tain_t deadline ;
  int notif = 0 ;
  PROG = "s6-rc-fdholder-filler" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "1t:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case '1': notif = 1 ; break ;
        case 't': if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        default : strerr_dieusage(100, USAGE) ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&deadline, t) ;
    else deadline = tain_infinite_relative ;
  }

  s6_fdholder_init(&a, 6) ;
  tain_now_g() ;
  tain_add_g(&deadline, &deadline) ;
  if (argc)
  {
    tain_t offset = { .sec = TAI_ZERO } ;
    int p[2] ;
    unsigned int n = argc ;
    unsigned int i = 0 ;
    s6_fdholder_fd_t dump[n<<1] ;
    for (; i < n ; i++)
    {
      unsigned int len = str_len(argv[i]) ;
      if (len + 12 > S6_FDHOLDER_ID_SIZE)
      {
        errno = ENAMETOOLONG ;
        strerr_diefu2sys(111, "create identifier for ", argv[i]) ;
      }
      if (pipe(p) < 0)
        strerr_diefu1sys(111, "create pipe") ;
      dump[i<<1].fd = p[0] ;
      tain_add_g(&dump[i<<1].limit, &tain_infinite_relative) ;
      offset.nano = i << 1 ;
      tain_add(&dump[i<<1].limit, &dump[i<<1].limit, &offset) ;
      byte_copy(dump[i<<1].id, 12, "pipe:s6rc-r-") ;
      byte_copy(dump[i<<1].id + 12, len + 1, argv[i]) ;
      dump[(i<<1)+1].fd = p[1] ;
      offset.nano = 1 ;
      tain_add(&dump[(i<<1)+1].limit, &dump[i<<1].limit, &offset) ;
      byte_copy(dump[(i<<1)+1].id, 12 + len, dump[i<<1].id) ;
      dump[(i<<1)+1].id[10] = 'w' ;
    }
    if (!s6_fdholder_setdump_g(&a, dump, n << 1, &deadline))
      strerr_diefu1sys(111, "transfer pipes") ;
  }
  if (notif) write(1, "\n", 1) ;
  return 0 ;
}
