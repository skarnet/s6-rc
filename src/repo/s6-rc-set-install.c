/* ISC license. */

#include <skalibs/bsdsnowflake.h>
#include <skalibs/nonposix.h>

#include <skalibs/stat.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>

#include <skalibs/uint64.h>
#include <skalibs/types.h>
#include <skalibs/posixplz.h>
#include <skalibs/prog.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/tai.h>
#include <skalibs/cspawn.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-set-install [ -v verbosity ] [ -c bootdb ] [ -l livedir ] [ -r repo ] [ -f convfile ] [ -b ] [ -K ] [ -e | -E ] set"
#define dieusage() strerr_dieusage(100, USAGE)

enum golb_e
{
  GOLB_BLOCK = 0x01,
  GOLB_KEEPOLD = 0x02,
  GOLB_NOUPDATE = 0x04,
  GOLB_NOFORCEESSENTIALS = 0x08,
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_BOOTDB,
  GOLA_LIVEDIR,
  GOLA_REPODIR,
  GOLA_CONVFILE,
  GOLA_N
} ;

int main (int argc, char const *const *argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'b', .lo = "block", .clear = 0, .set = GOLB_BLOCK },
    { .so = 'K', .lo = "keep-old", .clear = 0, .set = GOLB_KEEPOLD },
    { .so = 0, .lo = "no-update", .set = GOLB_NOUPDATE },
    { .so = 'e', .lo = "force-essentials", .clear = GOLB_NOFORCEESSENTIALS, .set = 0 },
    { .so = 'E', .lo = "no-force-essentials", .clear = 0, .set = GOLB_NOFORCEESSENTIALS },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'c', .lo = "bootdb", .i = GOLA_BOOTDB },
    { .so = 'l', .lo = "livedir", .i = GOLA_LIVEDIR },
    { .so = 'r', .lo = "repodir", .i = GOLA_REPODIR },
    { .so = 'f', .lo = "conversion-file", .i = GOLA_CONVFILE }
  } ;

  int fdlock ;
  unsigned int verbosity = 1 ;
  char const *wgola[GOLA_N] = { 0 } ;
  uint64_t wgolb = 0 ;
  unsigned int golc ;
  int r ;
  stralloc sa = STRALLOC_ZERO ;

  PROG = "s6-rc-set-install" ;
  wgola[GOLA_BOOTDB] = S6RC_BOOTDB ;
  wgola[GOLA_LIVEDIR] = S6RC_LIVEDIR ;
  wgola[GOLA_REPODIR] = S6RC_REPODIR ;

  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity needs to be an unsigned integer") ;
  if (wgola[GOLA_BOOTDB][0] != '/')
    strerr_dief2x(100, "bootdb", " needs to be an absolute path") ;
  if (wgola[GOLA_LIVEDIR][0] != '/')
    strerr_dief2x(100, "livedir", " needs to be an absolute path") ;
  if (!argc) dieusage() ;
  s6rc_repo_sanitize_setname(argv[0]) ;

  tain_now_g() ;
  fdlock = s6rc_repo_lock(wgola[GOLA_REPODIR], 0) ;
  if (fdlock == -1) strerr_diefu2sys(111, "lock ", wgola[GOLA_REPODIR]) ;
  r = s6rc_repo_checkset(wgola[GOLA_REPODIR], argv[0]) ;
  if (r) _exit(r) ;
 
  r = s6rc_repo_setuptodate(wgola[GOLA_REPODIR], argv[0]) ;
  if (r == -1) _exit(111) ;
  if (!r) strerr_dief3x(7, "set ", argv[0], " is not up-to-date; commit it first") ;

  if (!sadirname(&sa, wgola[GOLA_BOOTDB], strlen(wgola[GOLA_BOOTDB]))
   || !stralloc_0(&sa)) strerr_diefu1sys(111, "sadirname") ;
  sa.len-- ;

  {
    size_t repolen = strlen(wgola[GOLA_REPODIR]) ;
    size_t setlen = strlen(argv[0]) ;
    size_t clen = S6RC_REPO_COMPILE_BUFLEN(repolen, setlen) ;
    ssize_t l ;
    pid_t pid ;
    int wstat ;
    char *olddb = 0 ;
    char const *uargv[12] ;
    char fmtv[UINT_FMT] ;
    char clink[repolen + setlen + 11] ;
    char cfull[clen] ;
    char dstfn[sa.len + setlen + 36] ;
    fmtv[uint_fmt(fmtv, verbosity)] = 0 ;
    memcpy(clink, wgola[GOLA_REPODIR], repolen) ;
    memcpy(clink + repolen, "/compiled/", 10) ;
    memcpy(clink + repolen + 10, argv[0], setlen + 1) ;
    memcpy(cfull, clink, repolen + 10) ;
    memcpy(dstfn, sa.s, sa.len) ;
    dstfn[sa.len] = '/' ;
    l = readlink(clink, cfull + repolen + 10, setlen + 35) ;
    if (l == -1) strerr_diefu2sys(111, "readlink ", clink) ;
    if (l >= setlen + 35) strerr_diefu2x(102, "incorrect/unexpected link contents for ", clink) ;
    cfull[repolen + 10 + l] = 0 ;
    memcpy(dstfn + sa.len + 1, cfull + repolen + 10, l+1) ;
    l = 0 ;
    {    
      struct stat stsource, stdest ;
      if (stat(cfull, &stsource) == -1) strerr_diefu2sys(111, "stat ", cfull) ;
      if (stat(dstfn, &stdest) == -1)
      {
        if (errno != ENOENT)
          strerr_diefu2sys(111, "stat ", dstfn) ;
      }
      else if (stdest.st_mtim.tv_sec > stsource.st_mtim.tv_sec || (stdest.st_mtim.tv_sec == stsource.st_mtim.tv_sec && stdest.st_mtim.tv_nsec >= stsource.st_mtim.tv_nsec))
      {
        strerr_warni(0, "set ", argv[0], " is already installed as ", dstfn) ;
        _exit(0) ;
      }
      else strerr_dief(102, "huh? ", dstfn, " already exists") ;
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

    if (!(wgolb & GOLB_NOUPDATE))
    {
      uargv[l++] = S6RC_BINPREFIX "s6-rc-update" ;
      if (wgolb & GOLB_BLOCK) uargv[l++] = "-b" ;
      uargv[l++] = wgolb & GOLB_NOFORCEESSENTIALS ? "-E" : "-e" ;
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
        strerr_dief4x(r, uargv[0], " exited with code ", fmt, " (live database switch was NOT performed)") ;
      }
      if (olddb)
      {
        if (wgolb & GOLB_KEEPOLD)
        {
          if (buffer_puts(buffer_1small, olddb) == -1
           || !buffer_putflush(buffer_1small, "\n", 1))
            strerr_diefu2sys(111, "write to stdout", " (live database switch was performed)") ;
        }
        else rm_rf(olddb) ;
      }
      if (r)
      {
        char fmt[INT_FMT] ;
        fmt[int_fmt(fmt, r)] = 0 ;
        strerr_dief4x(r, uargv[0], " exited with code ", fmt, " (live database switch was performed") ;
      }
    }

    if (!atomic_symlink4(dstfn + sa.len + 1, wgola[GOLA_BOOTDB], 0, 0))
      strerr_diefu6sys(111, "symlink ", dstfn + sa.len + 1, " to ", wgola[GOLA_BOOTDB], " (live database switch was performed)", " (update that link manually or next boot might fail)") ;
  }
  // stralloc_free(&sa) ;
  _exit(0) ;
}
