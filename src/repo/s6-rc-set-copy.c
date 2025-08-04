/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/uint64.h>
#include <skalibs/posixplz.h>
#include <skalibs/prog.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-copy [ -v verbosity ] [ -r repo ] [ -f ] srcset dstset"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_FORCE,
  GOLB_N
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_REPODIR,
  GOLA_N
} ;

static gol_bool const rgolb[1] =
{
  { .so = 'f', .lo = "force", .clear = 0, .set = 1 << GOLB_FORCE }
} ;

static gol_arg const rgola[2] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR }
} ;


static uint64_t wgolb = 0 ;

static void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

static inline void docopy (char const *repo, char const *srcname, char const *dstname)
{
  size_t repolen = strlen(repo) ;
  size_t srclen = strlen(srcname) ;
  size_t dstlen = strlen(dstname) ;
  size_t r ;
  char src[repolen + srclen + 10] ;
  char dst[repolen + dstlen + 10] ;
  char realsrc[repolen + srclen + 18] ;
  char realdst[repolen + dstlen + 18] ;
  char olddst[repolen + dstlen + 18] ;
  memcpy(src, repo, repolen) ;
  memcpy(src + repolen, "/sources/", 9) ;
  memcpy(src + repolen + 9, srcname, srclen + 1) ;
  memcpy(dst, src, repolen + 9) ;
  memcpy(dst + repolen + 9, dstname, dstlen + 1) ;
  olddst[0] = 0 ;
  if (access(dst, F_OK) == -1)
  {
    if (errno != ENOENT) strerr_diefu2sys(111, "access ", dst) ;
  }
  else if (wgolb & (1 << GOLB_FORCE))
  {
    memcpy(olddst, dst, repolen + 9) ;
    r = readlink(dst, olddst + repolen + 9, dstlen + 9) ;
    if (r == -1) strerr_diefu2sys(111, "readlink ", dst) ;
    if (r != dstlen + 8) strerr_dief3x(102, "symlink ", dst, "doesn't point to a valid name") ;
  }
  else strerr_dief4x(100, "set ", dstname, " already exists in repository ", repo) ;

  memcpy(realsrc, src, repolen + 9) ;
  r = readlink(src, realsrc + repolen + 9, srclen + 9) ;
  if (r == -1) strerr_diefu2sys(111, "readlink ", src) ;
  if (r != srclen + 8) strerr_dief3x(102, "symlink ", src, "doesn't point to a valid name") ;
  realsrc[repolen + srclen + 17] = 0 ;
  memcpy(realdst, dst, repolen + 9) ;
  dst[repolen + 9] = '.' ;
  memcpy(realdst + repolen + 10, dstname, dstlen) ;
  memcpy(realdst + repolen + 10 + dstlen, realsrc + repolen + 10 + srclen, 8) ;
  if (!hiercopy(realsrc, realdst))
  {
    cleanup(realdst) ;
    strerr_diefu4sys(111, "copy ", realsrc, " to ", realdst) ;
  }
  if (!atomic_symlink4(realdst + repolen + 9, dst, 0, 0))
  {
    cleanup(realdst) ;
    strerr_diefu4sys(111, "symlink ", realdst + repolen + 9, " to ", dst) ;
  }
  if (olddst[0]) cleanup(olddst) ;
}

int main (int argc, char const *const *argv)
{
  char const *repo = S6RC_REPO_BASE ;
  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[2] = { 0 } ;
  unsigned int golc ;

  PROG = "s6-rc-set-copy" ;
  golc = gol_main(argc, argv, rgolb, 1, rgola, 2, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (wgola[GOLA_REPODIR]) repo = wgola[GOLA_REPODIR] ;
  if (argc < 2) dieusage() ;
  for (unsigned int i = 0 ; i < 2 ; i++)
    if (strchr(argv[i], '/') || strchr(argv[i], '\n'))
      strerr_dief1x(100, "set names cannot contain / or newlines") ;

  fdlock = s6rc_repo_lock(repo, 1) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", repo) ;
  tain_now_g() ;
  docopy(repo, argv[0], argv[1]) ;
  return 0 ;
}
