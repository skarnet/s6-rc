/* ISC license. */

#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/uint16.h>
#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/skamisc.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/s6rc-utils.h>
#include <s6-rc/repo.h>

static inline void cleanup (char const *ato, char const *bun)
{
  int e = errno ;
  rm_rf(ato) ;
  rm_rf(bun) ;
  errno = e ;
}

int s6rc_repo_sync (char const *repo, unsigned int verbosity, char const *fdhuser)
{
  size_t repolen = strlen(repo) ;
  char store[repolen + 13] ;
  char ato[repolen + 26] ;
  char bun[repolen + 26] ;

  memcpy(store, repo, repolen) ;
  memcpy(store + repolen, "/stores/", 8) ;
  store[repolen + 12] = 0 ;


 /* Fill new .atomics/ and .bundles/ with symlinks to real stores */

  memcpy(ato, repo, repolen) ;
  memcpy(ato + repolen, "/sources/..atomics:XXXXXX", 26) ;
  memcpy(bun, ato, repolen + 26) ;
  memcpy(bun + repolen + 11, "bundle", 6) ;
  if (!mkdtemp(ato))
  {
    strerr_warnfu2sys("mkdtemp ", ato) ;
    return 0 ;
  }
  if (chmod(ato, 02755) == -1)
  {
    strerr_warnfu2sys("chmod ", ato) ;
    return 0 ;
  }

  if (!mkdtemp(bun))
  {
    strerr_warnfu2sys("mkdtemp ", bun) ;
    rmdir(ato) ;
    return 0 ;
  }
  if (chmod(bun, 02755) == -1)
  {
    strerr_warnfu2sys("chmod ", bun) ;
    return 0 ;
  }

  for (uint16_t istore = 0 ;; istore++)
  {
    DIR *dir ;
    uint160_xfmt(store + repolen + 8, istore, 4) ;
    dir = opendir(store) ;
    if (!dir)
    {
      if (errno == ENOENT) break ;
      strerr_warnfu2sys("opendir ", store) ;
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
        char const *x ;
        char dst[repolen + 28 + len] ;
        char src[22 + len] ;
        memcpy(src, "../../../stores/", 16) ;
        memcpy(src + 16, store + repolen + 8, 4) ;
        src[20] = '/' ;
        memcpy(src + 21, d->d_name, len+1) ;
        switch (s6rc_type_check(-1, src))
        {
          case 1 :
          case 2 : x = ato ; break ;
          case 3 : x = bun ; break ;
          case 0 :
            strerr_warnf2x("invalid service type for ", src) ;
            dir_close(dir) ;
            goto err ;
          default :
            strerr_warnfu2sys("check service type of ", src) ;
            dir_close(dir) ;
            goto err ;
        }
        memcpy(dst, x, repolen + 26) ;
        dst[repolen + 26] = '/' ;
        memcpy(dst + repolen + 27, d->d_name, len+1) ;
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
            if (!sareadlink(&satmp, dst) || !stralloc_0(&satmp))
            {
              strerr_warnwu2sys("readlink ", dst) ;
              errno = EEXIST ;
              strerr_warnwu4sys("symlink ", src, " to ", dst) ;
            }
            else
              strerr_warnwu6x("symlink ", src, " to ", dst, ": already provided by ", satmp.s) ;
            satmp.len = 0 ;
          }
        }
      }
    }
    dir_close(dir) ;
    if (errno)
    {
      strerr_warnfu2sys("readdir ", store) ;
      goto err ;
    }
  }


 /* Compile the reference db - also checks consistency of new stores */

  {
    char const *subs[2] = { ato + repolen + 9, bun + repolen + 9 } ;
    char oldc[repolen + 49] ;
    int r = s6rc_repo_compile(repo, ".ref", subs, 2, oldc, verbosity, fdhuser) ;
    if (r <= 0) goto err ;
    if (r == 2) rm_rf(oldc) ;
  }


 /* Switch, delete links to old stores */

  {
    char fn[repolen + 18] ;
    char oldato[17] ;
    char oldbun[17] ;
    memcpy(fn, repo, repolen) ;
    memcpy(fn + repolen, "/sources/.atomics", 18) ;
    if (!atomic_symlink4(ato + repolen + 9, fn, oldato, 17)) goto err ;
    memcpy(fn + repolen + 10, "bundle", 6) ;
    if (!atomic_symlink4(bun + repolen + 9, fn, oldbun, 17))
    {
      strerr_warnfu4sys("symlink ", bun + repolen + 9, " to ", fn) ;
      memcpy(fn + repolen + 10, "atomic", 6) ;
      if (!atomic_symlink4(oldato, fn, 0, 0))
      {
        strerr_warnfu6sys("symlink ", oldato, " back to ", fn, " - weird race, manually check ", repo) ;
        goto err ;
      }
      goto err ;
    }
    memcpy(ato + repolen + 9, oldato, 16) ;
    memcpy(bun + repolen + 9, oldbun, 16) ;
    cleanup(ato, bun) ;
  }


 /* Update the existing sets */

  ato[repolen + 8] = 0 ;
  {
    stralloc sa = STRALLOC_ZERO ;
    genalloc ga = GENALLOC_ZERO ;  /* size_t */
    DIR *dir = opendir(ato) ;
    if (!dir)
    {
      strerr_warnfu2sys("opendir ", ato) ;
      return 0 ;
    }
    for (;;)
    {
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;
      if (!s6rc_repo_syncset_tmp(repo, d->d_name, &sa, &ga, verbosity)) break ;
    }
    dir_close(dir) ;
    genalloc_free(size_t, &ga) ;
    stralloc_free(&sa) ;
    if (errno)
    {
      strerr_warnfu2sys("readdir ", ato) ;
      return 0 ;
    }
  }
  
  return 1 ;

 err:
  cleanup(ato, bun) ;
  return 0 ;
}
