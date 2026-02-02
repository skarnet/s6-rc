/* ISC license. */

#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/uint16.h>
#include <skalibs/stat.h>
#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <skalibs/skamisc.h>
#include <skalibs/unix-transactional.h>

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
  mode_t m ;
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
  m = umask(0) ;
  if (!mkdtemp(ato))
  {
    strerr_warnfu2sys("mkdtemp ", ato) ;
    umask(m) ;
    return -1 ;
  }
  if (chmod(ato, 02755) == -1)
  {
    strerr_warnfu2sys("chmod ", ato) ;
    umask(m) ;
    return -1 ;
  }

  if (!mkdtemp(bun))
  {
    strerr_warnfu2sys("mkdtemp ", bun) ;
    rmdir(ato) ;
    umask(m) ;
    return -1 ;
  }
  umask(m) ;
  if (chmod(bun, 02755) == -1)
  {
    strerr_warnfu2sys("chmod ", bun) ;
    return -1 ;
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
        char dst[repolen + 27 + len] ;
        char src[19 + len] ;
        char oldsrc[19 + len] ;
        char fn[repolen + 14 + len] ;
        oldsrc[0] = 0 ;
        memcpy(src, "../../stores/", 13) ;
        memcpy(src + 13, store + repolen + 8, 4) ;
        src[17] = '/' ;
        memcpy(src + 18, d->d_name, len+1) ;
        memcpy(fn, store, repolen + 12) ;
        fn[repolen + 12] = '/' ;
        memcpy(fn + repolen + 13, d->d_name, len+1) ;
        switch (s6rc_repo_type_check(fn))
        {
          case 1 :
          case 2 : x = ato ; break ;
          case 3 : x = bun ; break ;
          case 0 :
            strerr_warnf2x("invalid service type for ", fn) ;
            dir_close(dir) ;
            goto err ;
          default :
            strerr_warnfu2sys("check service type of ", fn) ;
            dir_close(dir) ;
            goto err ;
        }
        memcpy(dst, x, repolen + 25) ;
        dst[repolen + 25] = '/' ;
        memcpy(dst + repolen + 26, d->d_name, len+1) ;
        if (!atomic_symlink4(src, dst, oldsrc, 19 + len))
        {
          strerr_warnfu4sys("symlink ", src, " to ", dst) ;
          dir_close(dir) ;
          goto err ;
        }
        if (oldsrc[0] && verbosity)
        {
          oldsrc[17] = 0 ;
          strerr_warnw6x("service ", d->d_name, " provided in store ", oldsrc + 13, "; overridden by new definition in store ", store + repolen + 8) ;
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
    char const *rxs[2] = { ato + repolen + 9, bun + repolen + 9 } ;
    char oldc[repolen + 49] ;
    int r = s6rc_repo_compile(repo, ".ref", rxs, 2, oldc, verbosity, fdhuser) ;
    if (r < 0) goto err ;
    if (!r) { cleanup(ato, bun) ; return 0 ; }
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
    if (oldato[0])
    {
      memcpy(ato + repolen + 9, oldato, 16) ;
      rm_rf(ato) ;
    }
    if (oldbun[0])
    {
      memcpy(bun + repolen + 9, oldbun, 16) ;
      rm_rf(bun) ;
    }
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
      return -1 ;
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
      return -1 ;
    }
  }
  
  return 1 ;

 err:
  cleanup(ato, bun) ;
  return -1 ;
}
