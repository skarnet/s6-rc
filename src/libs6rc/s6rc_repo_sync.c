/* ISC license. */

#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/s6rc-utils.h>
#include <s6-rc/repo.h>

static inline void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

int s6rc_repo_sync (char const *repo, char const *const *sources, size_t sourceslen, unsigned int verbosity)
{
  stralloc sa = STRALLOC_ZERO ;
  size_t repolen = strlen(repo) ;
  char newdir[repolen + 24] ;
  memcpy(newdir, repo, repolen) ;
  memcpy(newdir + repolen, "/.everything:cur:XXXXXX", 24) ;
  if (!mkdtemp(newdir))
  {
    strerr_warnfu2sys("mkdtemp ", newdir) ;
    return 0 ;
  }

  for (size_t i = 0 ; i < sourceslen ; i++)
  {
    size_t srclen = strlen(sources[i]) ;
    DIR *dir = opendir(sources[i]) ;
    if (!dir)
    {
      strerr_warnfu2sys("opendir ", sources[i]) ;
      goto err ;
    }
    for (;;)
    {
      size_t len ;
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;
      len = strlen(d->d_name) ;
      {
        char dst[repolen + 25 + len] ;
        char src[srclen + len + 2] ;
        memcpy(dst, newdir, repolen + 23) ;
        dst[repolen + 23] = '/' ;
        memcpy(dst + repolen + 24, d->d_name, len+1) ;
        memcpy(src, sources[i], srclen) ;
        src[srclen] = '/' ;
        memcpy(src + srclen + 1, d->d_name, len+1) ;
        switch (s6rc_type_check(-1, src))
        {
          case 1 :
          case 2 :
          case 3 : break ;
          case 0 :
            strerr_warnf2x("invalid service type for ", src) ;
            dir_close(dir) ;
            goto err ;
          default :
            strerr_warnfu2sys("check service type of ", src) ;
            dir_close(dir) ;
            goto err ;
        }
        if (symlink(src, dst) == -1)
        {
          if (errno != EEXIST)
          {
            strerr_warnfu4sys("symlink ", src, " to ", dst) ;
            dir_close(dir) ;
            goto err ;
          }
          if (verbosity)
          {
            sa.len = 0 ;
            if (!sareadlink(&sa, dst) || !stralloc_0(&sa))
            {
              strerr_warnwu2sys("readlink ", dst) ;
              errno = EEXIST ;
              strerr_warnwu4sys("symlink ", src, " to ", dst) ;
            }
            else
              strerr_warnwu6x("symlink ", src, " to ", dst, ": service is already provided by ", sa.s) ;
          }
        }
      }
    }
    dir_close(dir) ;
    if (errno)
    {
      strerr_warnfu2sys("readdir ", sources[i]) ;
      goto err ;
    }
  }

  if (chmod(newdir, 02755) == -1)
  {
    strerr_warnfu2sys("chmod ", newdir) ;
    goto err ;
  }

  {
    char curdir[repolen + 13] ;
    memcpy(curdir, repo, repolen) ;
    memcpy(curdir + repolen, "/.everything", 13) ;
    if (!atomic_symlink(newdir + repolen + 1, curdir, "tmp")) goto err ;
  }

  stralloc_free(&sa) ;
  return s6rc_repo_cleanup(repo) ;

 err:
  stralloc_free(&sa) ;
  cleanup(newdir) ;
  return 0 ;
}
