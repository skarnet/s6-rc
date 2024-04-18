/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/types.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/tai.h>
#include <skalibs/buffer.h>
#include <skalibs/stralloc.h>

#include <s6/fdholder.h>

#define USAGE "s6-rc-fdholder-filler [ -1 ] [ -t timeout ] < autofilled-filename"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "stralloc_catb")

#define N 16384

static inline unsigned int cclass (char c)
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

static inline char cnext (void)
{
  char c ;
  ssize_t r = buffer_get(buffer_0, &c, 1) ;
  if (r == -1) strerr_diefu1sys(111, "read from stdin") ;
  return r ? c : 0 ;
}

static inline unsigned int parse_servicenames (stralloc *sa, size_t *indices)
{
  static uint8_t const table[3][5] =
  {
    { 3, 0, 1, 0, 6 },
    { 3, 0, 1, 1, 1 },
    { 3, 8, 2, 2, 2 }
  } ;
  size_t pos = sa->len ;
  size_t n = 0 ;
  unsigned int state = 0 ;
  for (; state < 3 ; pos++)
  {
    char cur = cnext() ;
    uint8_t c = table[state][cclass(cur)] ;
    state = c & 3 ;
    if (c & 4)
    {
      if (n >= N) strerr_dief1x(1, "too many fds") ;
      indices[n++] = pos ;
    }
    if (c & 8) { if (!stralloc_0(sa)) dienomem() ; }
    else if (!stralloc_catb(sa, &cur, 1)) dienomem() ;
  }
  return n ;
}

int main (int argc, char const *const *argv)
{
  s6_fdholder_t a = S6_FDHOLDER_ZERO ;
  stralloc sa = STRALLOC_ZERO ;
  tain deadline ;
  int notif = 0 ;
  size_t n ;
  size_t indices[N] ;
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

  n = parse_servicenames(&sa, indices) ;
  if (n)
  {
    tain offset = { .sec = TAI_ZERO } ;
    int p[2] ;
    size_t i = 0 ;
    s6_fdholder_fd_t dump[n<<1] ;
    close(0) ;
    s6_fdholder_init(&a, 6) ;
    tain_now_set_stopwatch_g() ;
    tain_add_g(&deadline, &deadline) ;
    for (; i < n ; i++)
    {
      size_t len = strlen(sa.s + indices[i]) ;
      if (len + 12 > S6_FDHOLDER_ID_SIZE)
      {
        errno = ENAMETOOLONG ;
        strerr_diefu2sys(111, "create identifier for ", sa.s + indices[i]) ;
      }
      if (pipe(p) < 0)
        strerr_diefu1sys(111, "create pipe") ;
      dump[i<<1].fd = p[0] ;
      tain_add_g(&dump[i<<1].limit, &tain_infinite_relative) ;
      offset.nano = i << 1 ;
      tain_add(&dump[i<<1].limit, &dump[i<<1].limit, &offset) ;
      memcpy(dump[i<<1].id, "pipe:s6rc-r-", 12) ;
      memcpy(dump[i<<1].id + 12, sa.s + indices[i], len + 1) ;
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
