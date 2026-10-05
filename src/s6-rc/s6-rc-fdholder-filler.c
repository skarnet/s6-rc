/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/types.h>
#include <skalibs/gol.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/tai.h>
#include <skalibs/buffer.h>
#include <skalibs/djbunix.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6/fdholder.h>

#include <s6-rc/config.h>

#define USAGE "s6-rc-fdholder-filler [ -1 ] [ -t timeout ] [ -L catchall-logger ] < autofilled-filename"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "stralloc_catb")

enum golb_e
{
  GOLB_1 = 0x1,
} ;

enum gola_e
{
  GOLA_TIMEOUT,
  GOLA_CATCHALL,
  GOLA_N,
} ;

static inline uint8_t cclass (char c)
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

static inline void parse_servicenames (stralloc *sa, genalloc *g)
{
  static uint8_t const table[3][5] =
  {
    { 3, 0, 1, 0, 6 },
    { 3, 0, 1, 1, 1 },
    { 3, 8, 2, 2, 2 }
  } ;
  uint8_t state = 0 ;
  while (state < 3)
  {
    char cur = cnext() ;
    uint8_t c = table[state][cclass(cur)] ;
    state = c & 3 ;
    if (c & 4) if (!genalloc_append(size_t, g, &sa->len)) dienomem() ;
    if (c & 8) { if (!stralloc_0(sa)) dienomem() ; }
    else if (!stralloc_catb(sa, &cur, 1)) dienomem() ;
  }
}

int main (int argc, char const *const *argv)
{
  static gol_bool const rgolb[1] =
  {
    { .so = '1', .lo = "notify-stdout", .clear = 0, .set = GOLB_1 },
  } ;
  static gol_arg const rgola[GOLA_N] =
  {
    { .so = 't', .lo = "timeout", .i = GOLA_TIMEOUT },
    { .so = 'L', .lo = "catchall-logger", .i = GOLA_CATCHALL },
  } ;
  s6_fdholder_t a = S6_FDHOLDER_ZERO ;
  stralloc sa = STRALLOC_ZERO ;
  genalloc ga = GENALLOC_ZERO ; /* size_t */
  size_t n ;
  size_t const *indices ;
  tain deadline = TAIN_INFINITE_RELATIVE ;
  uint64_t wgolb = 0 ;
  char const *wgola[GOLA_N] = { 0 } ;
  tain offset = { .sec = TAI_ZERO } ;
  int p[2] ;
  size_t m = 0 ;
  PROG = "s6-rc-fdholder-filler" ;

  {
    unsigned int golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
    argc -= golc ; argv += golc ;
  }
  if (wgola[GOLA_TIMEOUT])
  {
    unsigned int t = 0 ;
    if (!uint0_scan(wgola[GOLA_TIMEOUT], &t))
      strerr_dief(100, "timeout must be an unsigned integer") ;
    if (t) tain_from_millisecs(&deadline, t) ;
  }
  if (wgola[GOLA_CATCHALL])
  {
    if (wgola[GOLA_CATCHALL][0] != '/')
      strerr_dief(100, "catchall-logger must be an absolute path") ;
  }

  parse_servicenames(&sa, &ga) ;
  n = genalloc_len(size_t, &ga) ;
  indices = genalloc_s(size_t, &ga) ;

  s6_fdholder_fd_t dump[1 + (n<<1)] ;

  close(0) ;
  s6_fdholder_init(&a, 6) ;
  tain_now_set_stopwatch_g() ;
  tain_add_g(&deadline, &deadline) ;

  if (wgola[GOLA_CATCHALL])
  {
    size_t len = strlen(wgola[GOLA_CATCHALL]) ;
    char fn[len + 6] ;
    memcpy(fn, wgola[GOLA_CATCHALL], len) ;
    memcpy(fn + len, "/fifo", 6) ;
    dump[m].fd = open_read(fn) ;
    if (dump[m].fd >= 0)
    {
      tain_add_g(&dump[m].limit, &tain_infinite_relative) ;
      memcpy(dump[m].id, "pipe:s6-rc-r/s6-svscan-log", 27) ;
      m++ ;
    }
  }
  for (size_t i = 0 ; i < n ; i++)
  {
    size_t len = strlen(sa.s + indices[i]) ;
    if (len + 12 > S6_FDHOLDER_ID_SIZE)
    {
      errno = ENAMETOOLONG ;
      strerr_diefusys(111, "create identifier for ", sa.s + indices[i]) ;
    }
    if (pipe(p) == -1) strerr_diefu1sys(111, "create pipe") ;
    dump[m].fd = p[0] ;
    tain_add_g(&dump[m].limit, &tain_infinite_relative) ;
    offset.nano = m ;
    tain_add(&dump[m].limit, &dump[m].limit, &offset) ;
    memcpy(dump[m].id, "pipe:s6rc-r-", 12) ;
    memcpy(dump[m].id + 12, sa.s + indices[i], len + 1) ;
    m++ ;
    dump[m].fd = p[1] ;
    offset.nano = 1 ;
    tain_add(&dump[m].limit, &dump[m-1].limit, &offset) ;
    memcpy(dump[m].id, dump[m-1].id, 13 + len) ;
    dump[m].id[10] = 'w' ;
    m++ ;
  }

  if (!s6_fdholder_setdump_g(&a, dump, m, &deadline))
    strerr_diefusys(111, "transfer pipes") ;

  if (wgolb & GOLB_1) write(1, "\n", 1) ;
  return 0 ;
}
