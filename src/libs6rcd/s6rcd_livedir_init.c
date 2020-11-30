/* ISC license. */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <skalibs/webipc.h>

#include "s6rcd.h"

#ifdef NAME_MAX
# define S6RC_NAME_MAX NAME_MAX
#else
# define S6RC_NAME_MAX 63
#endif

static inline int mksubdirs (char *s)
{
  size_t n = strlen(s) ;
  size_t i = 0 ;
  for (; i < n ; i++)
  {
    if (s[i] == '/')
    {
      int r ;
      s[i] = 0 ;
      r = mkdir(s, 0755) ;
      s[i] = '/' ;
      if (r < 0 && errno != EEXIST) break ;
    }
  }
  return i >= n ;
}

int s6rcd_livedir_init (char const *livedir, char const *scandir, char const *prefix, char const *compiled, int *sock)
{
  size_t plen = strlen(prefix) ;
  size_t llen = strlen(livedir) ;
  struct stat st ;
  char ltmp[llen + 9] ;
  if (plen >= S6RC_NAME_MAX)
    strerr_dief1x(100, "prefix is too long") ;

  memcpy(ltmp, livedir, llen + 1) ;
  if (!mksubdirs(ltmp, len))
    strerr_diefu2sys(111, "create subdirectories of ", s) ;
  if (mkdir(ltmp, 0755) < 0 && errno != EEXIST)
    strerr_diefu2sys(111, "mkdir ", ltmp) ;

  memcpy(ltmp + llen, "/s", 3) ;
  *sock = ipc_stream() ;
  if (*sock < 0)
    strerr_diefu1sys(111, "create socket") ;
  if (ipc_bind_reuse(*sock, ltmp) < 0)
    strerr_diefu2sys(111, "bind to ", ltmp) ;

  memcpy(ltmp + llen + 1, "scandir", 8) ;
  if (lstat(ltmp, &st) < 0)
  {
    if (errno != ENOENT)
      strerr_diefu2sys(111, "lstat ", ltmp) ;
    if (symlink(scandir, ltmp) < 0)
      strerr_diefu4sys(111, "symlink ", ltmp, " to ", scandir) ;
  }
  else
  {
    size_t slen = strlen(scandir) ;
    char stmp[slen + 1] ;
    if (!S_ISLNK(st.st_mode))
      strerr_dief3x(100, "file ", ltmp, " exists and is not a symbolic link") ;
    if (readlink(ltmp, stmp, slen + 1) < 0)
      strerr_diefu2sys(111, "readlink ", ltmp) ;
    if (strncmp(scandir, stmp, slen + 1))
      strerr_dief4x(100, "provided scandir ", scandir, " does not match the contents of existing ", ltmp) ;
  }

  memcpy(ltmp + llen + 1, "prefix", 7) ;
  if (stat(ltmp, &st) < 0)
  {
    if (errno != ENOENT)
      strerr_diefu2sys(111, "stat ", ltmp) ;
    if (!openwritenclose_unsafe(ltmp, prefix, strlen(prefix)))
      strerr_diefu2sys(111, "write prefix to ", ltmp) ;
  }
  else
  {
    if (!S_IFREG(st.st_mode))
      strerr_dief3x(100, "file ", ltmp, " exists and is not a regular file") ;
    if (st.st_size != plen)
      strerr_dief4x(100, "provided prefix ", prefix, " does not match the contents of existing ", ltmp) ;
    {
      char stmp[plen] ;
      ssize_t r = openreadnclose(ltmp, ptmp, plen) ;
      if (r != plen)
        strerr_diefu2sys(111, "read ", ltmp) ;
      if (memcmp(ptmp, prefix, plen))
      strerr_dief4x(100, "provided prefix ", prefix, " does not match the contents of existing ", ltmp) ;
    }
  }

  memcpy(ltmp + llen + 1, "live", 5) ;
  if (lstat(ltmp, &st) < 0)
  {
    char name[12] ;
    if (errno != ENOENT) strerr_diefu2sys(111, "lstat ", ltmp) ;
    if (!s6rcd_livesubdir_create(name, live, compiled))
      strerr_diefu2sys(111, "create live subdirectory in ", live) ;
    if (symlink(name, ltmp) < 0)
      strerr_diefu4sys(111, "symlink ", ltmp, " to ", name) ;
    return 0 ;
  }
  if (!S_ISLNK(st.st_mode))
    strerr_dief3x(100, "livesubdir ", ltmp, " exists and is not a symbolic link") ;
  if (stat(ltmp, &st) < 0)
    strerr_diefu2sys(111, "stat ", ltmp) ;
  if (!S_ISDIR(st.st_mode))
    strerr_dief4x(100, "livesubdir ", ltmp, " exists and is not a symbolic link", " to a directory") ;
  return 1 ;
}
