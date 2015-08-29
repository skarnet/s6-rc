/* ISC license. */

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <skalibs/uint16.h>
#include <skalibs/uint.h>
#include <skalibs/bytestr.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/stralloc.h>
#include <skalibs/direntry.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/skamisc.h>
#include <s6/s6-supervise.h>
#include <s6/ftrigw.h>
#include <s6/ftrigr.h>
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-init [ -c compiled ] [ -l live ] [ -t timeout ] scandir"
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_diefu1sys(111, "stralloc_catb")

static unsigned int llen ;

static void cleanup (void)
{
  int e = errno ;
  satmp.s[llen] = 0 ;
  unlink(satmp.s) ;
  satmp.s[llen] = '.' ;
  rm_rf_in_tmp(&satmp, 0) ;
  stralloc_free(&satmp) ;
  errno = e ;
}

int main (int argc, char const *const *argv)
{
  tain_t deadline, tto ;
  unsigned int dirlen ;
  char const *live = S6RC_LIVE_BASE ;
  char const *compiled = S6RC_COMPILED_BASE ;
  PROG = "s6-rc-init" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "c:l:t:", &l) ;
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
    strerr_dief2x(100, "compiled", " must be an absolute path") ;
  if (live[0] != '/')
    strerr_dief2x(100, "live", " must be an absolute path") ;
  if (argv[0][0] != '/')
    strerr_dief2x(100, "scandir", " must be an absolute path") ;

  if (!s6rc_sanitize_dir(&satmp, live, &dirlen)) dienomem() ;
  llen = satmp.len ;
  if (!stralloc_catb(&satmp, S6RC_LIVE_REAL_SUFFIX, sizeof(S6RC_LIVE_REAL_SUFFIX)))
    strerr_diefu1sys(111, "stralloc_catb") ;

  {
    int fdlock ;
    int fdcompiled ;
    s6rc_db_t db ;
    DIR *dir ;
    unsigned int maxlen = 0 ;
    unsigned int ndirs = 0 ;
    unsigned int n ;
    char lfn[llen + 13] ;
    char cfn[llen + 23] ;


   /* Create the real dir, lock it, symlink */

    if (mkdir(satmp.s, 0755) < 0) strerr_diefu2sys(111, "mkdir ", satmp.s) ;
    if (!s6rc_lock(satmp.s, 2, &fdlock, 0, 0, 0))
    {
      cleanup() ;
      strerr_diefu2sys(111, "take lock on ", satmp.s) ;
    }
    byte_copy(lfn, llen, live) ; lfn[llen] = 0 ;
    if (symlink(satmp.s + dirlen, lfn) < 0)
    {
      cleanup() ;
      strerr_diefu4sys(111, "symlink ", satmp.s + dirlen, " to ", lfn) ;
    }
    

  /* compiled */

    fdcompiled = open_readb(compiled) ;
    if (fdcompiled < 0)
    {
      cleanup() ;
      strerr_diefu2sys(111, "open ", compiled) ;
    }
    byte_copy(lfn + llen + 1, 9, "compiled") ;
    if (symlink(compiled, lfn) < 0)
    {
      cleanup() ;
      strerr_diefu4sys(111, "symlink ", compiled, " to ", lfn) ;
    }


  /* scandir */

    byte_copy(lfn + llen + 1, 8, "scandir") ;
    if (symlink(argv[0], lfn) < 0)
    {
      cleanup() ;
      strerr_diefu4sys(111, "symlink ", argv[0], " to ", lfn) ;
    }


   /* state */

    
    byte_copy(lfn + llen + 1, 6, "state") ;
    {
      register int r = s6rc_db_read_sizes(fdcompiled, &db) ;
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
        byte_zero(zero, n) ;
        if (!openwritenclose_unsafe(lfn, zero, n))
        {
          cleanup() ;
          strerr_diefu2sys(111, "write ", lfn) ;
        }
      }
    }


   /* servicedirs */

    byte_copy(lfn + llen + 1, 12, "servicedirs") ;
    byte_copy(cfn, llen + 1, lfn) ;
    byte_copy(cfn + llen + 1, 21, "compiled/servicedirs") ;
    if (!hiercopy(cfn, lfn))
    {
      cleanup() ;
      strerr_diefu4sys(111, "recursively copy ", cfn, " to ", lfn) ;
    }

    tain_now_g() ;
    tain_add_g(&deadline, &tto) ;
    dir = opendir(lfn) ;
    if (!dir)
    {
      cleanup() ;
      strerr_diefu2sys(111, "opendir ", lfn) ;
    }
    for (;;)
    {
      unsigned int thislen ;
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;
      thislen = str_len(d->d_name) ;
      if (thislen > maxlen) maxlen = thislen ;
      ndirs++ ;
    }
    if (errno)
    {
      int e = errno ;
      dir_close(dir) ;
      errno = e ;
      cleanup() ;
      strerr_diefu2sys(111, "readdir ", lfn) ;
    }
    if (ndirs)
    {
      ftrigr_t a = FTRIGR_ZERO ;
      gid_t gid = getgid() ;
      unsigned int i = 0 ;
      int r ;
      uint16 ids[ndirs] ;
      char srcfn[llen + 23 + maxlen] ; /* XXX: unsafe if dir is writable by non-root */
      char dstfn[llen + 9 + sizeof(S6_SVSCAN_CTLDIR "/control") + maxlen] ;
      rewinddir(dir) ;
      byte_copy(srcfn, llen + 12, lfn) ;
      srcfn[llen + 12] = '/' ;
      byte_copy(dstfn, llen, satmp.s) ;
      byte_copy(dstfn + llen, 8, "/scandir") ;
      dstfn[llen + 8] = '/' ;
      if (!ftrigr_startf_g(&a, &deadline))
      {
        int e = errno ;
        dir_close(dir) ;
        errno = e ;
        cleanup() ;
        strerr_diefu1sys(111, "start event listener process") ;
      }
      for (;;)
      {
        unsigned int thislen ;
        direntry *d ;
        errno = 0 ;
        d = readdir(dir) ;
        if (!d) break ;
        if (d->d_name[0] == '.') continue ;
        thislen = str_len(d->d_name) ;
        byte_copy(srcfn + llen + 13, thislen, d->d_name) ;
        byte_copy(srcfn + llen + 13 + thislen, 6, "/down") ;
        if (!touch(srcfn))
        {
          cleanup() ;
          strerr_diefu2sys(111, "touch ", srcfn) ;
        }
        byte_copy(srcfn + llen + 14 + thislen, 9, "log/down") ;
        if (!touch(srcfn))
        {
          cleanup() ;
          strerr_diefu2sys(111, "touch ", srcfn) ;
        }
        byte_copy(srcfn + llen + 14 + thislen, 6, "event") ;
        if (!ftrigw_fifodir_make(srcfn, gid, 0))
        {
          cleanup() ;
          strerr_diefu2sys(111, "make fifodir ", srcfn) ;
        }
        ids[i] = ftrigr_subscribe_g(&a, srcfn, "s", 0, &deadline) ;
        if (!ids[i])
        {
          int e = errno ;
          dir_close(dir) ;
          errno = e ;
          cleanup() ;
          strerr_diefu2sys(111, "subscribe to ", srcfn) ;
        }
        srcfn[llen + 13 + thislen] = 0 ;
        byte_copy(dstfn + llen + 9, thislen + 1, d->d_name) ;
        if (symlink(srcfn, dstfn) < 0)
        {
          int e = errno ;
          dir_close(dir) ;
          errno = e ;
          cleanup() ;
          strerr_diefu4sys(111, "symlink ", srcfn, " to ", dstfn) ;
        }
        i++ ;
      }
      if (errno)
      {
        int e = errno ;
        dir_close(dir) ;
        errno = e ;
        cleanup() ;
        strerr_diefu2sys(111, "readdir ", lfn) ;
      }
      dir_close(dir) ;
      byte_copy(dstfn + llen + 9, sizeof(S6_SVSCAN_CTLDIR "/control"), S6_SVSCAN_CTLDIR "/control") ;
      r = s6_svc_write(dstfn, "a", 1) ;
      if (r < 0)
      {
        cleanup() ;
        strerr_diefu2sys(111, "write to ", dstfn) ;
      }
      if (!r) strerr_warnw2x("s6-svscan not running on ", argv[0]) ;
      else
      {
        if (ftrigr_wait_and_g(&a, ids, ndirs, &deadline) < 0)
        {
          cleanup() ;
          strerr_diefu1sys(111, "wait for s6-supervise processes to come up") ;
        }
      }
      ftrigr_end(&a) ;
    }
    else dir_close(dir) ;
  }
  return 0 ;
}
