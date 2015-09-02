/* ISC license. */

#include <skalibs/tai.h>
#include <s6-rc/s6rc-servicedir.h>


static int s6rc_servicedir_manage (char const *live, unsigned char const *state, unsigned int nlong, tain_t const *deadline)
{
#if 0
  unsigned int livelen = str_len(live) ;
  char 
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
#endif
  return 0 ;
}
