/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <skalibs/posixplz.h>
#include <skalibs/uint64.h>
#include <skalibs/types.h>
#include <skalibs/prog.h>
#include <skalibs/gol.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6/supervise.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-init [ -c bootdb ] [ -l livedir ] [ -p prefix ] [ -t timeout ] [ -d ] [ -b ] scandir"
#define dieusage() strerr_dieusage(100, USAGE)

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

static void check_scandir (char const *sdir)
{
  int r = s6_svc_writectl(sdir, S6_SVSCAN_CTLDIR, "", 0) ;
  if (r == -1) strerr_diefusys(111, "check ", sdir) ;
  if (r == -2) strerr_diefsys(111, "invalid ", sdir, "/" S6_SVSCAN_CTLDIR, " scandir control directory") ;
  if (!r) strerr_dief(100, "s6-svscan is not running on ", sdir) ;
}

static int check_existing_s6rc (char const *sdir, char const *prefix, stralloc *sa)
{
  size_t sdirlen = strlen(sdir) ;
  size_t plen = strlen(prefix) ;
  size_t start = sa->len ;
  int r ;
  char fn[sdirlen + plen + S6RC_FDHOLDER_LEN + 2] ;
  memcpy(fn, sdir, sdirlen) ;
  fn[sdirlen] = '/' ;
  memcpy(fn + sdirlen + 1, prefix, plen) ;
  memcpy(fn + sdirlen + 1 + plen, S6RC_FDHOLDER, S6RC_FDHOLDER_LEN + 1) ;
  r = s6_svc_ok(fn) ;
  if (r == -1) strerr_diefusys(111, "check the existence of service ", fn) ;
  if (!r) return 0 ;
  if (sarealpath(sa, fn) == -1) strerr_diefusys(111, "realpath ", fn) ;
  if (sa->len - start < 14 + S6RC_FDHOLDER_LEN || memcmp(sa->s + sa->len - S6RC_FDHOLDER_LEN - 14, "/servicedirs/" S6RC_FDHOLDER, 14 + S6RC_FDHOLDER_LEN))
    strerr_dief(111, "service ", fn, " exists but is not part of an s6-rc database") ;
  sa->s[sa->len - S6RC_FDHOLDER_LEN - 14] = 0 ;
  return 1 ;
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
  uint64_t wgolb = 0 ;
  char const *wgola[GOLA_N] = { [GOLA_BOOTDB] = S6RC_BOOTDB, [GOLA_LIVEDIR] = S6RC_LIVEDIR, [GOLA_PREFIX] = "", [GOLA_TIMEOUT] = 0, } ;
  unsigned int golc ;

  PROG = "s6-rc-init" ;
  golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
  argc -= golc ; argv += golc ;
  if (!argc) dieusage() ;

  if (wgola[GOLA_TIMEOUT])
  {
    unsigned int t = 0 ;
    if (!uint0_scan(wgola[GOLA_TIMEOUT], &t))
      strerr_dief(100, "timeout", " must be an unsigned integer") ;
    if (t) tain_from_millisecs(&deadline, t) ;
  }

  if (!(wgolb & GOLB_DEREF) && wgola[GOLA_BOOTDB][0] != '/')
    strerr_dief(100, "bootdb", " must be an absolute path") ;
  if (wgola[GOLA_LIVEDIR][0] != '/')
    strerr_dief(100, "livedir", " must be an absolute path") ;
  if (argv[0][0] != '/')
    strerr_dief(100, "scandir", " must be an absolute path") ;
  if (strchr(wgola[GOLA_PREFIX], '/') || strchr(wgola[GOLA_PREFIX], '\n'))
    strerr_dief(100, "prefix", " cannot contain a / or a newline") ;

  tain_now_set_stopwatch_g() ;
  tain_add_g(&deadline, &deadline) ;

  if (!s6rc_livedir_canon(&wgola[GOLA_LIVEDIR]))
    strerr_diefu1sys(111, "canonicalize livedir") ;
  if (wgolb & GOLB_DEREF)
  {
    char *x = realpath(wgola[GOLA_BOOTDB], 0) ;
    if (!x) strerr_diefu2sys(111, "realpath ", wgola[GOLA_BOOTDB]) ;
    wgola[GOLA_BOOTDB] = x ;
  }

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

  if (s6rc_servicedir_manage_g(wgola[GOLA_LIVEDIR], wgola[GOLA_PREFIX], &deadline) < 0)
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
