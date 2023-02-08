/* ISC license. */

#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

#include <skalibs/direntry.h>
#include <skalibs/djbunix.h>

#include <s6/servicedir.h>

#include <s6-rc/s6rc-utils.h>
#include <s6-rc/s6rc-servicedir.h>
#include "s6rc-servicedir-internal.h"

int s6rc_servicedir_copy_online (char const *src, char const *dst)
{
  size_t srclen = strlen(src) ;
  size_t dstlen = strlen(dst) ;
  unsigned int n ;
  unsigned int i = 0 ;
  int wantup = 0 ;
  int e ;
  struct stat st ;
  DIR *dir ;
  char srcfn[srclen + S6_SERVICEDIR_FILE_MAXLEN + 6] ;
  char dstfn[dstlen + S6_SERVICEDIR_FILE_MAXLEN + 6] ;
  char oldfn[dstlen + S6_SERVICEDIR_FILE_MAXLEN + 6] ;
  memcpy(srcfn, src, srclen) ;
  srcfn[srclen] = '/' ;
  memcpy(dstfn, dst, dstlen + 1) ;
  memcpy(oldfn, dst, dstlen) ;
  memcpy(oldfn + dstlen, "/old", 5) ;
  if (rm_rf(oldfn) < 0 && errno != ENOENT) return 0 ;
  {
    mode_t m = umask(0) ;
    int r = mkdir(oldfn, 0755) ;
    umask(m) ;
    if (r < 0) return 0 ;
  }
  dstfn[dstlen] = '/' ;
  oldfn[dstlen + 4] = '/' ;
  wantup = s6rc_servicedir_block(dst) ;
  if (wantup < 0) { e = errno ; goto errdir ; }

  for (; s6rc_servicedir_file_list[i].name ; i++)
  {
    strcpy(dstfn + dstlen + 1, s6rc_servicedir_file_list[i].name) ;
    strcpy(oldfn + dstlen + 5, s6rc_servicedir_file_list[i].name) ;
    if (rename(dstfn, oldfn) < 0
     && (errno != ENOENT || s6rc_servicedir_file_list[i].options & S6_SVFILE_MANDATORY))
    {
      e = errno ;
      goto errrename ;
    }
  }
  n = i ;
  while (i--)
  {
    if (!s6rc_servicedir_copy_one(src, dst, s6rc_servicedir_file_list + i))
    {
      e = errno ;
      goto errremove ;
    }
  }
  oldfn[dstlen + 4] = 0 ;
  rm_rf(oldfn) ;

 /* New files copied, now let's update the instances with the new template */

  memcpy(oldfn + dstlen + 1, "template", 9) ;
  if (stat(oldfn, &st) == 0)
  {
    if (!S_ISDIR(st.st_mode)) { errno = EINVAL ; goto erri ; }
    memcpy(dstfn + dstlen + 1, "instances", 10) ;
    dir = opendir(dstfn) ;
    if (!dir)
    {
      mode_t m ;
      if (errno != ENOENT) goto erri ;
      m = umask(0) ;
      if (mkdir(dstfn, 0755) == -1) { umask(m) ; goto erri ; }
      dstfn[dstlen + 9] = 0 ;
      if (mkdir(dstfn, 0755) == -1) { umask(m) ; goto erri ; }
      umask(m) ;
    }
    else
    {
      dstfn[dstlen + 10] = '/' ;
      for (;;)
      {
        direntry *d ;
        errno = 0 ;
        d = readdir(dir) ;
        if (!d) break ;
        if (d->d_name[0] == '.')
          if (((d->d_name[1] == '.') && !d->d_name[2]) || !d->d_name[1])
            continue ;
        {
          size_t len = strlen(d->d_name) ;
          char ifn[dstlen + 12 + len] ;
          memcpy(ifn, dstfn, dstlen + 11) ;
          memcpy(ifn + dstlen + 11, d->d_name, len + 1) ;
          if (!s6rc_servicedir_copy_online(oldfn, ifn)) goto errd ;
        }
      }
      dir_close(dir) ;
      if (errno) goto erri ;
    }
  }
  else if (errno != ENOENT) goto erri ;

  s6rc_servicedir_unblock(dst, wantup) ;
  return 1 ;

 errremove:
  for (; i < n ; i++)
  {
    strcpy(dstfn + dstlen + 1, s6rc_servicedir_file_list[i].name) ;
    rm_rf(dstfn) ;
  }
  i = n ;
 errrename:
  while (i--)
  {
    strcpy(dstfn + dstlen + 1, s6rc_servicedir_file_list[i].name) ;
    strcpy(oldfn + dstlen + 5, s6rc_servicedir_file_list[i].name) ;
    rename(oldfn, dstfn) ;
  }
  s6rc_servicedir_unblock(dst, wantup) ;
 errdir:
  oldfn[dstlen + 4] = 0 ;
  rmdir(oldfn) ;
  errno = e ;
  return 0 ;

 errd:
  dir_close(dir) ;
 erri:
  e = errno ;
  s6rc_servicedir_unblock(dst, wantup) ;
  errno = e ;
  return 0 ;
}
