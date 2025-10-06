/* ISC license. */

#include <skalibs/bsdsnowflake.h>

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>

#include <skalibs/uint64.h>
#include <skalibs/stat.h>
#include <skalibs/types.h>
#include <skalibs/posixplz.h>
#include <skalibs/prog.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/djbunix.h>
#include <skalibs/cspawn.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-install [ -v verbosity ] [ -l livedir ] [ -r repo ] [ -f convfile ] [ -b ] [ -K ] set bootdbdir"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_BLOCK = 0x01,
  GOLB_KEEPOLD = 0x02
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_LIVEDIR,
  GOLA_REPODIR,
  GOLA_CONVFILE,
  GOLA_N
} ;

static gol_bool const rgolb[] =
{
  { .so = 'b', .lo = "block", .clear = 0, .set = GOLB_BLOCK },
  { .so = 'K', .lo = "keep-old", .clear = 0, .set = GOLB_KEEPOLD }
} ;

static gol_arg const rgola[] =
{
  { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
  { .so = 'l', .lo = "livedir", .i = GOLA_LIVEDIR },
  { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR },
  { .so = 'f', .lo = "conversion-file", .i = GOLA_CONVFILE }
} ;

int main (int argc, char const *const *argv)
{
  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;
  int r ;

  PROG = "s6-rc-set-install" ;
  wgola[GOLA_LIVEDIR] = S6RC_LIVE_BASE ;
  wgola[GOLA_REPODIR] = S6RC_REPO_BASE ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (argc < 2) dieusage() ;
  if (strchr(argv[0], '/') || strchr(argv[0], '\n'))
    strerr_dief1x(100, "set names cannot contain / or newlines") ;

  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 0) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;
  r = s6rc_repo_checkset(wgola[GOLA_REPODIR], argv[0]) ;
  if (r) _exit(r) ;
 
  r = s6rc_repo_setuptodate(wgola[GOLA_REPODIR], argv[0]) ;
  if (r == -1) _exit(111) ;
  if (!r) strerr_dief3x(1, "set ", argv[0], " is not up-to-date; commit it first") ;

  {
    size_t repolen = strlen(wgola[GOLA_REPODIR]) ;
    size_t setlen = strlen(argv[0]) ;
    size_t clen = S6RC_REPO_COMPILE_BUFLEN(repolen, setlen) ;
    size_t dstlen = strlen(argv[1]) ;
    ssize_t l ;
    pid_t pid ;
    int wstat ;
    char *olddb = 0 ;
    char const *uargv[11] ;
    char fmtv[UINT_FMT] ;
    char clink[repolen + setlen + 11] ;
    char cfull[clen] ;
    char dstfn[dstlen + setlen + 36] ;
    fmtv[uint_fmt(fmtv, verbosity)] = 0 ;
    memcpy(clink, wgola[GOLA_REPODIR], repolen) ;
    memcpy(clink + repolen, "/compiled/", 10) ;
    memcpy(clink + repolen + 10, argv[0], setlen + 1) ;
    memcpy(cfull, clink, repolen + 10) ;
    memcpy(dstfn, argv[1], dstlen) ;
    dstfn[dstlen] = '/' ;
    l = readlink(clink, cfull + repolen + 10, setlen + 35) ;
    if (l == -1) strerr_diefu2sys(111, "readlink ", clink) ;
    if (l >= setlen + 35) strerr_diefu2x(102, "incorrect/unexpected link contents for ", clink) ;
    cfull[repolen + 10 + l] = 0 ;
    memcpy(dstfn + dstlen + 1, cfull + repolen + 10, l+1) ;
    l = 0 ;
    {    
      struct stat st ;
      if (stat(argv[1], &st) == -1)
        strerr_diefu2sys(111, "stat ", argv[1]) ;
      if (!S_ISDIR(st.st_mode))
        strerr_dief2x(100, argv[1], " is not a directory") ;
      if (stat(dstfn, &st) == -1)
      {
        if (errno != ENOENT)
          strerr_diefu2sys(111, "stat ", dstfn) ;
      }
      else strerr_dief2x(102, dstfn, " already exists!?") ;
    }
    {
      size_t llen = strlen(wgola[GOLA_LIVEDIR]) ;
      char cfn[llen + 10] ;
      memcpy(cfn, wgola[GOLA_LIVEDIR], llen) ;
      memcpy(cfn + llen, "/compiled", 10) ;
      olddb = realpath(cfn, 0) ;
      if (!olddb && errno != ENOENT) strerr_diefu2sys(111, "realpath ", cfn) ;
    }

    if (!hiercopy(cfull, dstfn))
    {
      int e = errno ;
      rm_rf(dstfn) ;
      errno = e ;
      strerr_diefu4sys(111, "recursively copy ", cfull, " to ", dstfn) ;
    }

    uargv[l++] = S6RC_BINPREFIX "s6-rc-update" ;
    if (wgolb & GOLB_BLOCK) uargv[l++] = "-b" ;
    uargv[l++] = "-v" ;
    uargv[l++] = fmtv ;
    uargv[l++] = "-l" ;
    uargv[l++] = wgola[GOLA_LIVEDIR] ;
    if (wgola[GOLA_CONVFILE])
    {
      uargv[l++] = "-f" ;
      uargv[l++] = wgola[GOLA_CONVFILE] ;
    }
    uargv[l++] = "--" ;
    uargv[l++] = dstfn ;
    uargv[l++] = 0 ;

    pid = cspawn(uargv[0], uargv, (char const *const *)environ, 0, 0, 0) ;
    if (!pid)
    {
      int e = errno ;
      rm_rf(dstfn) ;
      errno = e ;
      strerr_diefu2sys(111, "spawn ", uargv[0]) ;
    }
    r = wait_pid(pid, &wstat) ;
    if (r == -1) strerr_diefu3sys(111, "wait for the ", uargv[0], " process (updating may yet succeed)") ;
    if (WIFSIGNALED(wstat))
    {
      char fmt[INT_FMT] ;
      fmt[int_fmt(fmt, WTERMSIG(wstat))] = 0 ;
      strerr_dief3x(wait_estatus(wstat), uargv[0], " crashed with signal ", fmt) ;
    }
    r = WEXITSTATUS(wstat) ;
    if (r == 111)
      strerr_dief2x(111, uargv[0], " exited 111, unable to know the state of the live db or clean up") ;
    if ((r >= 3 && r <= 10) || r == 100)
    {
      int e = errno ;
      char fmt[INT_FMT] ;
      fmt[int_fmt(fmt, r)] = 0 ;
      rm_rf(dstfn) ;
      errno = e ;
      strerr_dief4x(r, uargv[0], " exited with code ", fmt, ": live database switch NOT performed") ;
    }
    if (olddb)
    {
      if (wgolb & GOLB_KEEPOLD)
      {
        if (buffer_puts(buffer_1small, olddb) == -1
         || !buffer_putflush(buffer_1small, "\n", 1))
          strerr_diefu1sys(111, "write to stdout (live database switch performed)") ;
      }
      else rm_rf(olddb) ;
    }
    if (r)
    {
      char fmt[INT_FMT] ;
      fmt[int_fmt(fmt, r)] = 0 ;
      strerr_dief4x(r, uargv[0], " exited with code ", fmt, ", but the live database switch was performed") ;
    }
  }
  _exit(0) ;
}
