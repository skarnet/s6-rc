/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/types.h>
#include <skalibs/strerr.h>
#include <skalibs/sgetopt.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/tai.h>

#include <s6/fdholder.h>

#define USAGE "s6-rc-fdholder-filler [ -1 ] [ -t timeout ] < autofilled-filename"
#define dieusage() strerr_dieusage(100, USAGE)

#define N 4096

static inline unsigned int class (char c)
{
  switch (c)
  {
    case 0 : return 0 ;
    case '\n' : return 1 ;
    case '#' : return 2 ;
    case ' ' :
    case '\r' :
    case '\t' : return 3 ;
    default : return 4 ;
  }
}

static inline unsigned int parse_servicenames (char *s, unsigned int *indices)
{
  static unsigned char const table[3][5] =
  {
    { 3, 0, 1, 0, 6 },
    { 3, 0, 1, 1, 1 },
    { 3, 8, 2, 2, 2 }
  } ;
  unsigned int pos = 0 ;
  unsigned int n = 0 ;
  unsigned int state = 0 ;
  for (; state < 3 ; pos++)
  {
    unsigned char c = table[state][class(s[pos])] ;
    state = c & 3 ;
    if (c & 4) indices[n++] = pos ;
    if (c & 8) s[pos] = 0 ;
  }
  return n ;
}

int main (int argc, char const *const *argv)
{
  s6_fdholder_t a = S6_FDHOLDER_ZERO ;
  tain deadline ;
  int notif = 0 ;
  unsigned int n ;
  unsigned int indices[N] ;
  char buf[N<<1] ;
  PROG = "s6-rc-fdholder-filler" ;
  {
    unsigned int t = 0 ;
    subgetopt l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "1t:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case '1': notif = 1 ; break ;
        case 't': if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&deadline, t) ;
    else deadline = tain_infinite_relative ;
  }

  {
    size_t r = allread(0, buf, N<<1) ;
    if (r >= N<<1) strerr_dief3x(100, "file ", argv[0], " is too big") ;
    buf[r] = 0 ;
  }
  n = parse_servicenames(buf, indices) ;
  if (n)
  {
    tain offset = { .sec = TAI_ZERO } ;
    int p[2] ;
    unsigned int i = 0 ;
    s6_fdholder_fd_t dump[n<<1] ;
    close(0) ;
    s6_fdholder_init(&a, 6) ;
    tain_now_set_stopwatch_g() ;
    tain_add_g(&deadline, &deadline) ;
    for (; i < n ; i++)
    {
      size_t len = strlen(buf + indices[i]) ;
      if (len + 12 > S6_FDHOLDER_ID_SIZE)
      {
        errno = ENAMETOOLONG ;
        strerr_diefu2sys(111, "create identifier for ", buf + indices[i]) ;
      }
      if (pipe(p) < 0)
        strerr_diefu1sys(111, "create pipe") ;
      dump[i<<1].fd = p[0] ;
      tain_add_g(&dump[i<<1].limit, &tain_infinite_relative) ;
      offset.nano = i << 1 ;
      tain_add(&dump[i<<1].limit, &dump[i<<1].limit, &offset) ;
      memcpy(dump[i<<1].id, "pipe:s6rc-r-", 12) ;
      memcpy(dump[i<<1].id + 12, buf + indices[i], len + 1) ;
      dump[(i<<1)+1].fd = p[1] ;
      offset.nano = 1 ;
      tain_add(&dump[(i<<1)+1].limit, &dump[i<<1].limit, &offset) ;
      memcpy(dump[(i<<1)+1].id, dump[i<<1].id, 13 + len) ;
      dump[(i<<1)+1].id[10] = 'w' ;
    }
    if (!s6_fdholder_setdump_g(&a, dump, n << 1, &deadline))
      strerr_diefu1sys(111, "transfer pipes") ;
  }
  if (notif) write(1, "\n", 1) ;
  return 0 ;
}
