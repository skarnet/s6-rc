/* ISC license. */

#include <skalibs/bsdsnowflake.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <skalibs/posixplz.h>
#include <skalibs/uint64.h>
#include <skalibs/types.h>
#include <skalibs/prog.h>
#include <skalibs/gol.h>
#include <skalibs/stat.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/tai.h>
#include <skalibs/direntry.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6/supervise.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-init [ -c bootdb ] [ -l livedir ] [ -p prefix ] [ -t timeout ] [ -d ] [ -b ] scandir"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefusys(111, "stralloc_catb")

enum golb_e
{
  GOLB_DEREF = 0x01,
  GOLB_BLOCK = 0x02,
} ;

enum gola_e
{
  GOLA_BOOTDB,
  GOLA_LIVEDIR,
  GOLA_PREFIX,
  GOLA_TIMEOUT,
  GOLA_N
} ;

static void cleanup (stralloc *sa)
{
  int e = errno ;
  rm_rf_in_tmp(sa, 0) ;
  errno = e ;
}

static inline void check_scandir (char const *sdir)
{
  int r = s6_svc_writectl(sdir, S6_SVSCAN_CTLDIR, "", 0) ;
  if (r == -1) strerr_diefusys(111, "check ", sdir) ;
  if (r == -2) strerr_diefsys(111, "invalid ", sdir, "/" S6_SVSCAN_CTLDIR, " scandir control directory") ;
  if (!r) strerr_dief(100, "s6-svscan is not running on ", sdir) ;
}

static int check_livedir (char const *live, char const *sdir, char const *prefix, char const *bootdb, stralloc *sa)
{
  size_t llen = strlen(live) ;
  size_t plen = strlen(prefix) ;
  size_t w ;
  int r ;
  char fn[llen + 14 + S6RC_FDHOLDER_LEN] ;
  char pr[plen + 1] ;

  memcpy(fn, live, llen) ;
  memcpy(fn + llen, "/servicedirs/" S6RC_FDHOLDER, 14 + S6RC_FDHOLDER_LEN) ;
  r = s6_svc_ok(fn) ;
  if (r == -1)
  {
    if (errno != ENOENT) strerr_diefusys(111, "check ", fn) ;
    fn[llen] = 0 ;
    strerr_warnw("directory ", fn, " looks like a live directory but has no ", "s6rc-fdholder service") ;
    return 0 ;
  }
  else if (!r)
  {
    fn[llen] = 0 ;
    strerr_warnw("directory ", fn, " looks like a live directory but has no ", "supervisors running") ;
    return 0 ;
  }

  memcpy(fn + llen, "/scandir", 9) ;
  if (sareadlink(sa, fn) == -1 || !stralloc_0(sa)) strerr_diefusys(111, "readlink ", fn) ;
  sa->len = 0 ;
  if (strcmp(sa->s, sdir))
  {
    fn[llen] = 0 ;
    strerr_dief(102, "existing livedir at ", fn, " uses scandir at ", sa->s, " rather than ", sdir) ;
  }

  memcpy(fn + llen, "/prefix", 8) ;
  w = openreadnclose(fn, pr, plen+1) ;
  if (w == -1) strerr_diefusys(111, "read ", fn) ;
  if (w != plen || strncmp(pr, prefix, plen))
  {
    fn[llen] = 0 ;
    strerr_dief(102, "prefix mismatch with the existing livedir at ", fn) ;
  } 

  memcpy(fn + llen, "/compiled", 10) ;
  if (sareadlink(sa, fn) == -1 || !stralloc_0(sa)) strerr_diefusys(111, "readlink ", fn) ;
  sa->len = 0 ;
  if (strcmp(sa->s, bootdb))
  {
    fn[llen] = 0 ;
    strerr_dief(102, "existing livedir at ", fn, " uses bootdb at ", sa->s, " rather than ", bootdb) ;
  }
  return 1 ;
}

static inline size_t find_livedir (char const *live, size_t llen, size_t lpos, char const *sdir, char const *prefix, char const *bootdb, stralloc *sa)
{
  DIR *dir ;
  char fn[llen + 7] ;
  memcpy(fn, live, llen) ;
  memcpy(fn + llen, ":s6-rc-", 7) ;
  fn[lpos] = 0 ;
  dir = opendir(fn) ;
  if (!dir) strerr_diefusys(111, "opendir ", fn) ;
  for (;;)
  {
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    if (strncmp(d->d_name, fn + lpos + 1, llen + 6 - lpos)) continue ;

    size_t sublen = strlen(d->d_name) ;
    char subfn[lpos + sublen + 2] ;
    memcpy(subfn, live, lpos) ;
    subfn[lpos] = '/' ;
    memcpy(subfn + lpos + 1, d->d_name, sublen + 1) ;
    if (check_livedir(subfn, sdir, prefix, bootdb, sa))
    {
      dir_close(dir) ;
      if (!stralloc_catb(sa, subfn, lpos + sublen + 2)) dienomem() ;
      return 1 ;
    }
  }
  if (errno) strerr_diefusys(111, "readdir ", sa->s) ;
  dir_close(dir) ;
  return 0 ;
}

int main (int argc, char const *const *argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 0, .lo = "no-dereference", .clear = GOLB_DEREF, .set = 0 },
    { .so = 'd', .lo = "dereference", .clear = 0, .set = GOLB_DEREF },
    { .so = 0, .lo = "no-block", .clear = GOLB_BLOCK, .set = 0 },
    { .so = 'b', .lo = "block", .clear = 0, .set = GOLB_BLOCK },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'c', .lo = "bootdb", .i = GOLA_BOOTDB },
    { .so = 'l', .lo = "livedir", .i = GOLA_LIVEDIR },
    { .so = 'p', .lo = "prefix", .i = GOLA_PREFIX },
    { .so = 't', .lo = "timeout", .i = GOLA_TIMEOUT },
  } ;
  tain deadline = TAIN_INFINITE_RELATIVE ;
  stralloc sa = STRALLOC_ZERO ;
  size_t dirlen ;
  char *x ;
  uint64_t wgolb = 0 ;
  char const *wgola[GOLA_N] = { [GOLA_BOOTDB] = S6RC_BOOTDB, [GOLA_LIVEDIR] = S6RC_LIVEDIR, [GOLA_PREFIX] = "", [GOLA_TIMEOUT] = 0, } ;
  unsigned int golc ;

  PROG = "s6-rc-init" ;
  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (!argc) dieusage() ;

  if (argv[0][0] != '/') strerr_dief(100, "scandir", " must be an absolute path") ;
  if (wgola[GOLA_TIMEOUT])
  {
    unsigned int t = 0 ;
    if (!uint0_scan(wgola[GOLA_TIMEOUT], &t))
      strerr_dief(100, "timeout", " must be an unsigned integer") ;
    if (t) tain_from_millisecs(&deadline, t) ;
  }
  if (wgolb & GOLB_DEREF)
  {
    x = realpath(wgola[GOLA_BOOTDB], 0) ;
    if (!x) strerr_diefusys(111, "realpath ", wgola[GOLA_BOOTDB]) ;
    wgola[GOLA_BOOTDB] = x ;
  }
  else if (wgola[GOLA_BOOTDB][0] != '/') strerr_dief(100, "bootdb", " must be an absolute path") ;
  if (strchr(wgola[GOLA_PREFIX], '/') || strchr(wgola[GOLA_PREFIX], '\n'))
    strerr_dief(100, "prefix", " cannot contain a / or a newline") ;
  if (!s6rc_livedir_canon(&wgola[GOLA_LIVEDIR]))
    strerr_diefusys(111, "canonicalize ", wgola[GOLA_LIVEDIR]) ;

  check_scandir(argv[0]) ;
  x = realpath(wgola[GOLA_LIVEDIR], 0) ;
  if (x)
  {
    if (check_livedir(x, argv[0], wgola[GOLA_PREFIX], wgola[GOLA_BOOTDB], &sa))
    {
      strerr_warni("s6-rc already running on ", x) ;
      _exit(0) ;
    }
    free(x) ;
  }
  else
  {
    size_t llen = strlen(wgola[GOLA_LIVEDIR]) ;
    char *d = strrchr(wgola[GOLA_LIVEDIR], '/') ;
    if (!d || d == wgola[GOLA_LIVEDIR]) strerr_dief(102, "invalid existing livedir: ", wgola[GOLA_LIVEDIR]) ;
    dirlen = d - wgola[GOLA_LIVEDIR] ;
    if (find_livedir(wgola[GOLA_LIVEDIR], llen, dirlen, argv[0], wgola[GOLA_PREFIX], wgola[GOLA_BOOTDB], &sa))
    {
      strerr_warni("s6-rc already running on ", sa.s, " - recovering it") ;
      if (!atomic_symlink4(sa.s + dirlen + 1, wgola[GOLA_LIVEDIR], 0, 0))
        strerr_diefusys(111, "symlink ", sa.s + dirlen + 1, " to ", wgola[GOLA_LIVEDIR]) ;
      _exit(0) ;
    }
  }

  tain_now_set_stopwatch_g() ;
  tain_add_g(&deadline, &deadline) ;

  {
    s6rc_db_t db ;
    int r ;
    int fdcompiled = open_readb(wgola[GOLA_BOOTDB]) ;
    if (fdcompiled == -1)
      strerr_diefusys(111, "open ", wgola[GOLA_BOOTDB]) ;
    r = s6rc_db_read_sizes(fdcompiled, &db) ;
    if (r == -1)
      strerr_diefusys(111, "read database size in ", wgola[GOLA_BOOTDB]) ;
    else if (!r)
      strerr_dief(4, "invalid database size in ", wgola[GOLA_BOOTDB]) ;
    close(fdcompiled) ;
    {
      unsigned char state[db.nshort + db.nlong] ;
      memset(state, 0, db.nshort + db.nlong) ;
      if (!s6rc_livedir_create(&sa, wgola[GOLA_LIVEDIR], PROG, argv[0], wgola[GOLA_PREFIX], wgola[GOLA_BOOTDB], state, db.nshort + db.nlong, &dirlen))
        strerr_diefusys(111, "create live directory") ;
    }
  }
  {
    size_t clen = strlen(wgola[GOLA_BOOTDB]) ;
    char lfn[sa.len + 13] ;
    char cfn[clen + 13] ;
    memcpy(lfn, sa.s, sa.len) ;
    memcpy(lfn + sa.len, "/servicedirs", 13) ;
    memcpy(cfn, wgola[GOLA_BOOTDB], clen) ;
    memcpy(cfn + clen, "/servicedirs", 13) ;
    sa.len++ ;
    if (!hiercopy(cfn, lfn))
    {
      cleanup(&sa) ;
      strerr_diefusys(111, "recursively copy ", cfn, " to ", lfn) ;
    }
  }
  if (!atomic_symlink4(sa.s + dirlen, wgola[GOLA_LIVEDIR], 0, 0))
  {
    cleanup(&sa) ;
    strerr_diefusys(111, "symlink ", sa.s + dirlen, " to ", wgola[GOLA_LIVEDIR]) ;
  }

  if (s6rc_servicedir_manage_g(sa.s, wgola[GOLA_PREFIX], &deadline) < 0)
  {
    unlink_void(wgola[GOLA_LIVEDIR]) ;
    cleanup(&sa) ;
    if (errno == ENXIO)
      strerr_diefu(100, "supervise service directories in ", wgola[GOLA_LIVEDIR], "/servicedirs", ": s6-svscan not running on ", argv[0]) ;
    else
      strerr_diefusys(111, "supervise service directories in ", wgola[GOLA_LIVEDIR], "/servicedirs") ;
  }
  _exit(0) ;
}
