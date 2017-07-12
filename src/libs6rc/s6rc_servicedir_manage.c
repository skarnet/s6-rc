/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <skalibs/direntry.h>
#include <skalibs/djbunix.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <s6/s6-supervise.h>
#include <s6/ftrigr.h>
#include <s6/ftrigw.h>
#include <s6-rc/s6rc-servicedir.h>

static void rollback (char const *live, char const *s, size_t len)
{
  while (len)
  {
    size_t n = strlen(s) + 1 ;
    s6rc_servicedir_unsupervise(live, s, 0) ;
    s += n ; len -= n ;
  }
}

int s6rc_servicedir_manage (char const *live, tain_t const *deadline, tain_t *stamp)
{
  ftrigr_t a = FTRIGR_ZERO ;
  stralloc newnames = STRALLOC_ZERO ;
  genalloc ids = GENALLOC_ZERO ; /* uint16_t */
  gid_t gid = getgid() ;
  size_t livelen = strlen(live) ;
  int ok = 1 ;
  int e = 0 ;
  DIR *dir ;
  char dirfn[livelen + 13] ;
  if (!ftrigr_startf(&a, deadline, stamp)) return 0 ;
  memcpy(dirfn, live, livelen) ;
  memcpy(dirfn + livelen, "/servicedirs", 13) ;
  dir = opendir(dirfn) ;
  if (!dir) goto closederr ;
  for (;;)
  {
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    {
      size_t len = strlen(d->d_name) ;
      int fdlock ;
      int r ;
      uint16_t id ;
      char srcfn[livelen + 20 + len] ;
      char dstfn[livelen + 10 + len] ;
      memcpy(srcfn, dirfn, livelen + 12) ;
      srcfn[livelen + 12] = '/' ;
      memcpy(srcfn + livelen + 13, d->d_name, len + 1) ;
      fdlock = s6_svc_lock_take(srcfn) ;
      if (fdlock < 0) goto err ;
      r = s6_svc_ok(srcfn) ;
      if (r < 0) goto errinloop ;
      if (!r)
      {
        memcpy(srcfn + livelen + 13 + len, "/down", 6) ;
        if (!touch(srcfn)) goto errinloop ;
        memcpy(srcfn + livelen + 14 + len, "event", 6) ;
        if (!ftrigw_fifodir_make(srcfn, gid, 0)) goto errinloop ;
        id = ftrigr_subscribe(&a, srcfn, "s", 0, deadline, stamp) ;
        if (!id) goto errinloop ;
        s6_svc_lock_release(fdlock) ;
        if (!genalloc_append(uint16_t, &ids, &id)) goto err ;
        srcfn[livelen + 13 + len] = 0 ;
      }
      else s6_svc_lock_release(fdlock) ;
      memcpy(dstfn, live, livelen) ;
      memcpy(dstfn + livelen, "/scandir/", 9) ;
      memcpy(dstfn + livelen + 9, d->d_name, len + 1) ;
      if (symlink(srcfn, dstfn) < 0)
      {
        if (!r || errno != EEXIST) goto err ;
      }
      else if (!r)
      {
        if (!stralloc_catb(&newnames, d->d_name, len + 1))
        {
          e = errno ;
          s6rc_servicedir_unsupervise(live, d->d_name, 0) ;
          goto errn ;
        }
      }
      continue ;
     errinloop:
      e = errno ;
      s6_svc_lock_release(fdlock) ;
      goto errn ;
    }
  }
  if (errno) goto err ;
  dir_close(dir) ;
  {
    char scanfn[livelen + 9] ;
    int r ;
    memcpy(scanfn, live, livelen) ;
    memcpy(scanfn + livelen, "/scandir", 9) ;
    r = s6_svc_writectl(scanfn, S6_SVSCAN_CTLDIR, "a", 1) ;
    if (r < 0) goto closederr ;
    if (!r) ok = 3 ;
    else if (ftrigr_wait_and(&a, genalloc_s(uint16_t, &ids), genalloc_len(uint16_t, &ids), deadline, stamp) < 0)
      goto closederr ;
  }

  ftrigr_end(&a) ;
  genalloc_free(uint16_t, &ids) ;
  stralloc_free(&newnames) ;
  return ok ;

 err:
  e = errno ;
 errn:
  dir_close(dir) ;
  goto closederrn ;
 closederr:
  e = errno ;
 closederrn:
  ftrigr_end(&a) ;
  genalloc_free(uint16_t, &ids) ;
  rollback(live, newnames.s, newnames.len) ;
  stralloc_free(&newnames) ;
  errno = e ;  
  return 0 ;
}
