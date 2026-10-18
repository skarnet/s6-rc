/* ISC license. */

#include <skalibs/bsdsnowflake.h>
#include <skalibs/nonposix.h>

#include <skalibs/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/uint64.h>
#include <skalibs/types.h>
#include <skalibs/posixplz.h>
#include <skalibs/prog.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr.h>
#include <skalibs/gol.h>
#include <skalibs/stralloc.h>
#include <skalibs/direntry.h>
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
  GOLB_NOSNAP = 0x10,
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

#define DSTFNLEN(bootdir, set) (strlen(bootdir) + strlen(set) + 36)

static void remove_snapshots_except (char const *repo, char const *snap)
{
  DIR *dir ;
  size_t repolen = strlen(repo) ;
  char src[repolen + 9] ;
  memcpy(src, repo, repolen) ;
  memcpy(src + repolen, "/sources", 9) ;
  dir = opendir(src) ;
  if (!dir) strerr_diefusys(111, "opendir ", src) ;
  for (;;)
  {
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (strncmp(d->d_name, ".snapshot.", 10)) continue ;
    if (!strcmp(d->d_name, snap)) continue ;
    s6rc_repo_setdelete(repo, d->d_name) ;
  }
  if (errno) strerr_diefusys(111, "readdir ", src) ;
  dir_close(dir) ;
}

static int install_needs_upgrade (char const *livedir)
{
  size_t llen = strlen(livedir) ;
  char livedb[llen + 10] ;
  memcpy(livedb, livedir, llen) ;
  memcpy(livedb + llen, "/compiled", 10) ;
  return s6rc_repo_read_major(livedb) < s6rc_get_major(S6RC_VERSION) ;
}

static void snapcompile (char const *livedir, char const *repo, unsigned int verbosity, stralloc *sa)
{
  int r ;
  size_t llen = strlen(livedir) ;
  size_t snappos = sa->len, fdhuserpos, catchallpos, defbundlepos ;
  char fn[llen + 28] ;
  memcpy(fn, livedir, llen) ;
  memcpy(fn + llen, "/compiled", 10) ;
  if (!stralloc_catb(sa, ".snapshot.", 10)) strerr_diefusys(111, "stralloc_catb") ;
  if (sareadlink(sa, fn) == -1 || !stralloc_0(sa)) strerr_diefusys(111, "readlink ", fn) ;
  fdhuserpos = sa->len ;
  memcpy(fn + llen + 9, "metainfo/fdhuser", 17) ;
  if (!openslurpnclose(fn, sa, 4096) || !stralloc_0(sa)) strerr_diefusys(111, "read ", fn) ;
  catchallpos = sa->len ;
  memcpy(fn + llen + 18, "catchall", 9) ;
  if (!openslurpnclose(fn, sa, 4096) || !stralloc_0(sa)) strerr_diefusys(111, "read ", fn) ;
  defbundlepos = sa->len ;
  memcpy(fn + llen + 18, "defbundle", 10) ;
  if (!openslurpnclose(fn, sa, 4096) || !stralloc_0(sa)) strerr_diefusys(111, "read ", fn) ;

  size_t oldclen = S6RC_REPO_COMPILE_BUFLEN(strlen(repo), fdhuserpos - 1) ;
  char oldc[oldclen] ;

  r = s6rc_repo_setcompile(repo, sa->s + snappos, sa->s + defbundlepos, oldc, verbosity, sa->s + fdhuserpos, sa->s + catchallpos, 1) ;
  if (r == -1) _exit(111) ;
  if (!r) _exit(1) ;
  if (r == 2) rm_rf(oldc) ;
  sa->len = fdhuserpos ;
}

static void copy_compiled_to_bootdir (char *dstfn, char const *repo, char const *set, char const *bootdir, char const *livedir, stralloc *olddbsa)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t bootdirlen = strlen(bootdir) ;
  size_t clen = S6RC_REPO_COMPILE_BUFLEN(repolen, setlen) ;
  ssize_t l ;
  char clink[repolen + setlen + 11] ;
  char cfull[clen] ;
  memcpy(clink, repo, repolen) ;
  memcpy(clink + repolen, "/compiled/", 10) ;
  memcpy(clink + repolen + 10, set, setlen + 1) ;
  memcpy(cfull, clink, repolen + 10) ;
  memcpy(dstfn, bootdir, bootdirlen) ;
  dstfn[bootdirlen] = '/' ;
  l = readlink(clink, cfull + repolen + 10, setlen + 35) ;
  if (l == -1) strerr_diefusys(111, "readlink ", clink) ;
  if (l >= setlen + 35) strerr_dief(102, "incorrect/unexpected link contents for ", clink) ;
  cfull[repolen + 10 + l] = 0 ;
  memcpy(dstfn + bootdirlen + 1, cfull + repolen + 10, l+1) ;
  l = 0 ;

  {
    struct stat stsource, stdest ;
    if (stat(cfull, &stsource) == -1) strerr_diefusys(111, "stat ", cfull) ;
    if (stat(dstfn, &stdest) == -1)
    {
      if (errno != ENOENT)
        strerr_diefusys(111, "stat ", dstfn) ;
    }
    else if (stdest.st_mtim.tv_sec > stsource.st_mtim.tv_sec || (stdest.st_mtim.tv_sec == stsource.st_mtim.tv_sec && stdest.st_mtim.tv_nsec >= stsource.st_mtim.tv_nsec))
    {
      strerr_warni("set ", set, " is already installed as ", dstfn) ;
      _exit(0) ;
    }
    else strerr_dief(102, "huh? ", dstfn, " already exists") ;
  }

  {
    size_t llen = strlen(livedir) ;
    char cfn[llen + 10] ;
    memcpy(cfn, livedir, llen) ;
    memcpy(cfn + llen, "/compiled", 10) ;
    if (sarealpath(olddbsa, cfn) == -1 && errno != ENOENT)
      strerr_diefusys(111, "realpath ", cfn) ;
    if (!stralloc_0(olddbsa)) strerr_diefusys(111, "stralloc_catb") ;
  }

  if (!hiercopy(cfull, dstfn))
  {
    int e = errno ;
    rm_rf(dstfn) ;
    errno = e ;
    strerr_diefusys(111, "recursively copy ", cfull, " to ", dstfn) ;
  }
}

int main (int argc, char const *const *argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'b', .lo = "block", .clear = 0, .set = GOLB_BLOCK },
    { .so = 'K', .lo = "keep-old", .clear = 0, .set = GOLB_KEEPOLD },
    { .so = 0, .lo = "no-update", .set = GOLB_NOUPDATE },
    { .so = 'e', .lo = "force-essentials", .clear = GOLB_NOFORCEESSENTIALS, .set = 0 },
    { .so = 'E', .lo = "no-force-essentials", .clear = 0, .set = GOLB_NOFORCEESSENTIALS },
    { .so = 0, .lo = "snapshot", .clear = GOLB_NOSNAP, .set = 0 },
    { .so = 0, .lo = "no-snapshot", .clear = 0, .set = GOLB_NOSNAP },
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
  stralloc bootdirsa = STRALLOC_ZERO ;

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
 
  r = s6rc_repo_setuptodate(wgola[GOLA_REPODIR], argv[0], !!(wgolb & GOLB_NOSNAP)) ;
  if (r == -1) _exit(111) ;
  if (!r) strerr_dief3x(7, "set ", argv[0], " is not up-to-date; commit it first") ;

  if (!sadirname(&bootdirsa, wgola[GOLA_BOOTDB], strlen(wgola[GOLA_BOOTDB]))
   || !stralloc_0(&bootdirsa)) strerr_diefu1sys(111, "sadirname") ;
  bootdirsa.len-- ;

  {
    int needupgrade = install_needs_upgrade(wgola[GOLA_LIVEDIR]) ;
    stralloc olddbsa = STRALLOC_ZERO ;
    char dstfn[DSTFNLEN(bootdirsa.s, argv[0])] ;
    char fmtv[UINT_FMT] ;
    fmtv[uint_fmt(fmtv, verbosity)] = 0 ;


   /* If we need a format-upgrade and the user isn't opposed to it */
    if (needupgrade && !(wgolb & GOLB_NOUPDATE) && !(wgolb & GOLB_NOSNAP))
    {
      pid_t pid ;
      int wstat ;
      char const *uargv[9] ;
      unsigned int l = 0 ;
      char snapfn[DSTFNLEN(bootdirsa.s, olddbsa.s)] ;

     /* We find the snapshot corresponding to the current live db and we compile it */
      snapcompile(wgola[GOLA_LIVEDIR], wgola[GOLA_REPODIR], verbosity, &olddbsa) ;
      olddbsa.len = 0 ;

     /* We copy the compiled snapshot to the bootdir. It has the correct major. */
      copy_compiled_to_bootdir(snapfn, wgola[GOLA_REPODIR], olddbsa.s, bootdirsa.s, wgola[GOLA_LIVEDIR], &olddbsa) ;

     /* We perform the format-upgrade */
      uargv[l++] = S6RC_BINPREFIX "s6-rc-format-upgrade" ;
      uargv[l++] = "-v" ;
      uargv[l++] = fmtv ;
      uargv[l++] = "-l" ;
      uargv[l++] = wgola[GOLA_LIVEDIR] ;
      if (wgolb & GOLB_BLOCK) uargv[l++] = "-b" ;
      uargv[l++] = "--" ;
      uargv[l++] = snapfn ;
      uargv[l++] = 0 ;
      pid = cspawn(uargv[0], uargv, (char const *const *)environ, 0, 0, 0) ;
      if (!pid)
      {
        int e = errno ;
        rm_rf(snapfn) ;
        errno = e ;
        strerr_diefusys(111, "spawn ", uargv[0]) ;
      }
      r = wait_pid(pid, &wstat) ;
      if (r == -1) strerr_diefusys(111, "wait for the ", uargv[0], " process") ;
      if (WIFSIGNALED(wstat))
      {
        char fmt[INT_FMT] ;
        fmt[int_fmt(fmt, WTERMSIG(wstat))] = 0 ;
        strerr_dief(wait_estatus(wstat), uargv[0], " crashed with signal ", fmt) ;
      }
      if (WEXITSTATUS(wstat))
      {
        char fmt[INT_FMT] ;
        fmt[int_fmt(fmt, WEXITSTATUS(wstat))] = 0 ;
        strerr_dief(wait_estatus(wstat), uargv[0], " exited with signal ", fmt) ;
      }

     /* Now the live db has the same major version number as the new db we want. */

      if (!atomic_symlink4(snapfn + bootdirsa.len + 1, wgola[GOLA_BOOTDB], 0, 0))
        strerr_diefusys(111, "symlink ", snapfn + bootdirsa.len + 1, " to ", wgola[GOLA_BOOTDB], " (live database switch was performed to snapshot, not to new set)", " (update that link manually then retry the command)") ;
      if (olddbsa.s[0]) rm_rf(olddbsa.s) ;  /* we don't keep the db with the obsolete major */
      olddbsa.len = 0 ;

     /* The compiled snapshot is now in place and serves as the db to update from. */
    }

   /* We copy the compiled committed set to the bootdir */
    copy_compiled_to_bootdir(dstfn, wgola[GOLA_REPODIR], argv[0], bootdirsa.s, wgola[GOLA_LIVEDIR], &olddbsa) ;

    if (!(wgolb & GOLB_NOUPDATE))
    {
      pid_t pid ;
      int wstat ;
      char const *uargv[12] ;
      unsigned int l = 0 ;

     /* If --no-snapshot AND we need an upgrade, we assume the user knows what they're doing
        and the dbs represent the same state: so we just format-upgrade directly to the new compiled db */
      if (needupgrade && wgolb & GOLB_NOSNAP)
      {
        uargv[l++] = S6RC_BINPREFIX "s6-rc-format-upgrade" ;
        uargv[l++] = "-v" ;
        uargv[l++] = fmtv ;
        uargv[l++] = "-l" ;
        uargv[l++] = wgola[GOLA_LIVEDIR] ;
        if (wgolb & GOLB_BLOCK) uargv[l++] = "-b" ;
        uargv[l++] = "--" ;
        uargv[l++] = dstfn ;
        uargv[l++] = 0 ;
        if (verbosity)
          strerr_warnw("installing set without a snapshot after a major version update; assuming the same service state") ;
      }
      else
     /* We know the new db has the same major as the live db (either naturally or after a format-upgrade
        via a snapshot), but we don't know if they represent the same state. This is the common case.
        So we s6-rc-update, to switch live dbs. */
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
      }

      pid = cspawn(uargv[0], uargv, (char const *const *)environ, 0, 0, 0) ;
      if (!pid)
      {
        int e = errno ;
        rm_rf(dstfn) ;
        errno = e ;
        strerr_diefusys(111, "spawn ", uargv[0]) ;
      }
      r = wait_pid(pid, &wstat) ;
      if (r == -1) strerr_diefusys(111, "wait for the ", uargv[0], " process (updating may yet succeed)") ;
      if (WIFSIGNALED(wstat))
      {
        char fmt[INT_FMT] ;
        fmt[int_fmt(fmt, WTERMSIG(wstat))] = 0 ;
        strerr_dief(wait_estatus(wstat), uargv[0], " crashed with signal ", fmt) ;
      }
      r = WEXITSTATUS(wstat) ;
      if (!needupgrade || !(wgolb & GOLB_NOSNAP))
      {
        if (r == 111)
          strerr_dief(111, uargv[0], " exited 111, unable to know the state of the live db or clean up") ;
        if ((r >= 3 && r <= 10) || r == 100)
        {
          int e = errno ;
          char fmt[INT_FMT] ;
          fmt[int_fmt(fmt, r)] = 0 ;
          rm_rf(dstfn) ;
          errno = e ;
          strerr_dief(r, uargv[0], " exited with code ", fmt, " (live database switch was NOT performed)") ;
        }
      }
      if (r)
      {
        char fmt[INT_FMT] ;
        fmt[int_fmt(fmt, r)] = 0 ;
        strerr_dief(r, uargv[0], " exited with code ", fmt, " (live database switch was performed") ;
      }
    }

    if (!atomic_symlink4(dstfn + bootdirsa.len + 1, wgola[GOLA_BOOTDB], 0, 0))
      strerr_diefusys(111, "symlink ", dstfn + bootdirsa.len + 1, " to ", wgola[GOLA_BOOTDB], " (live database switch was performed)", " (update that link manually or next boot might fail)") ;

    if (olddbsa.s[0])
    {
      if (wgolb & GOLB_KEEPOLD)
      {
        if (buffer_puts(buffer_1small, olddbsa.s) == -1
         || !buffer_putflush(buffer_1small, "\n", 1))
          strerr_diefusys(111, "write to stdout", " (live database switch was performed)") ;
      }
      else rm_rf(olddbsa.s) ;
    }

    remove_snapshots_except(wgola[GOLA_REPODIR], dstfn + bootdirsa.len + 1) ;
  }
  _exit(0) ;
}
