/* ISC license. */

#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <skalibs/types.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/stralloc.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/skamisc.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-init [ -c compiled ] [ -l live ] [ -t timeout ] scandir"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "stralloc_catb")

static size_t llen ;

static void cleanup (void)
{
  int e = errno ;
  satmp.s[llen] = 0 ;
  unlink(satmp.s) ;
  satmp.s[llen] = ':' ;
  rm_rf_in_tmp(&satmp, 0) ;
  stralloc_free(&satmp) ;
  errno = e ;
}

int main (int argc, char const *const *argv)
{
  tain_t deadline, tto ;
  size_t dirlen ;
  char const *live = S6RC_LIVE_BASE ;
  char const *compiled = S6RC_COMPILED_BASE ;
  PROG = "s6-rc-init" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "c:l:t:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'c' : compiled = l.arg ; break ;
        case 'l' : live = l.arg ; break ;
        case 't' : if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&tto, t) ;
    else tto = tain_infinite_relative ;
  }
  if (!argc) dieusage() ;

  if (compiled[0] != '/')
    strerr_dief2x(100, compiled, " is not an absolute path") ;
  if (live[0] != '/')
    strerr_dief2x(100, live, " is not an absolute path") ;
  if (argv[0][0] != '/')
    strerr_dief2x(100, argv[0], " is not an absolute path") ;

  tain_now_g() ;
  tain_add_g(&deadline, &tto) ;

  if (!s6rc_sanitize_dir(&satmp, live, &dirlen)) dienomem() ;
  llen = satmp.len ;
  if (!stralloc_cats(&satmp, ":initial") || !stralloc_0(&satmp))
    strerr_diefu1sys(111, "stralloc_catb") ;

  {
    int fdlock ;
    int fdcompiled ;
    int ok ;
    s6rc_db_t db ;
    unsigned int n ;
    char lfn[llen + 13] ;
    char cfn[llen + 23] ;


   /* Create the real dir, lock it, symlink */

    unlink(live) ;
    rm_rf(satmp.s) ;
    if (mkdir(satmp.s, 0755) < 0) strerr_diefu2sys(111, "mkdir ", satmp.s) ;
    if (!s6rc_lock(satmp.s, 2, &fdlock, 0, 0, 0))
    {
      char tmp[satmp.len] ;
      memcpy(tmp, satmp.s, satmp.len) ;
      cleanup() ;
      strerr_diefu2sys(111, "take lock on ", tmp) ;
    }
    memcpy(lfn, satmp.s, llen) ;
    lfn[llen] = 0 ;
    if (symlink(satmp.s + dirlen, lfn) < 0)
    {
      char tmp[satmp.len - dirlen] ;
      memcpy(tmp, satmp.s + dirlen, satmp.len - dirlen) ;
      cleanup() ;
      strerr_diefu4sys(111, "symlink ", tmp, " to ", lfn) ;
    }
    

   /* compiled */

    fdcompiled = open_readb(compiled) ;
    if (fdcompiled < 0)
    {
      cleanup() ;
      strerr_diefu2sys(111, "open ", compiled) ;
    }
    memcpy(lfn + llen, "/compiled", 10) ;
    if (symlink(compiled, lfn) < 0)
    {
      cleanup() ;
      strerr_diefu4sys(111, "symlink ", compiled, " to ", lfn) ;
    }


   /* scandir */

    memcpy(lfn + llen + 1, "scandir", 8) ;
    if (symlink(argv[0], lfn) < 0)
    {
      cleanup() ;
      strerr_diefu4sys(111, "symlink ", argv[0], " to ", lfn) ;
    }


   /* state */

    memcpy(lfn + llen + 1, "state", 6) ;
    {
      int r = s6rc_db_read_sizes(fdcompiled, &db) ;
      if (r <= 0)
      {
        cleanup() ;
        if (r < 0) strerr_diefu2sys(111, "read database size in ", compiled) ;
        else strerr_dief2x(4, "invalid database size in ", compiled) ;
      }
      close(fdcompiled) ;
      n = db.nshort + db.nlong ;
      {
        char zero[n] ;
        memset(zero, 0, n) ;
        if (!openwritenclose_unsafe(lfn, zero, n))
        {
          cleanup() ;
          strerr_diefu2sys(111, "write ", lfn) ;
        }
      }
    }


   /* servicedirs */

    memcpy(lfn + llen + 1, "servicedirs", 12) ;
    memcpy(cfn, lfn, llen + 1) ;
    memcpy(cfn + llen + 1, "compiled/servicedirs", 21) ;
    if (!hiercopy(cfn, lfn))
    {
      cleanup() ;
      strerr_diefu4sys(111, "recursively copy ", cfn, " to ", lfn) ;
    }


   /* start the supervisors */

    lfn[llen] = 0 ;
    ok = s6rc_servicedir_manage_g(lfn, &deadline) ;
    if (!ok)
    {
      cleanup() ;
      strerr_diefu3sys(111, "supervise service directories in ", lfn, "/servicedirs") ;
    }
    if (ok & 2)
      strerr_warnw3x("s6-svscan not running on ", lfn, "/scandir") ;
  }  
  return 0 ;
}
