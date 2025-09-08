/* ISC license. */

#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

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

static int unlink_stales_in_sub (char const *repo, char const *set, char const *sub, uint32_t where, stralloc *sa, genalloc *ga, unsigned int verbosity)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t sublen = strlen(sub) ;
  size_t subfnlen = repolen + setlen + sublen + 10 ;
  char subfn[subfnlen + 1] ;
  memcpy(subfn, repo, repolen) ;
  memcpy(subfn + repolen, "/sources/", 9) ;
  memcpy(subfn + repolen + 9, set, setlen) ;
  subfn[repolen + 9 + setlen] = '/' ;
  memcpy(subfn + repolen + 10 + setlen, sub, sublen + 1) ;
  DIR *dir = opendir(subfn) ;
  if (!dir)
  {
    strerr_warnfu2sys("opendir ", subfn) ;
    return 0 ;
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
    char fn[subfnlen + len + 2] ;
    memcpy(fn, subfn, sublen) ;
    fn[sublen] = '/' ;
    memcpy(fn + sublen + 1, d->d_name, len+1) ;
    if (access(fn, F_OK) == -1)
    {
      if (errno != ENOENT)
      {
        strerr_warnfu2sys("access ", fn) ;
        goto err ;
      }
      unlink_void(fn) ;
      if (verbosity >= 2)
        strerr_warni6x("service ", d->d_name, " does not exist anymore, removed from sub ", sub, " of set ", set) ;
    }
    else
    {
      if (!genalloc_append(size_t, ga, &sa->len)) goto err ;
      if (!stralloc_catb(sa, d->d_name, len+1)) goto err ;
    }
  }
  if (errno)
  {
    strerr_warnfu2sys("readdir ", subfn) ;
    goto err ;
  }

  dir_close(dir) ;
  return 1 ;

 err:
  dir_close(dir) ;
  return 0 ;
}

static inline int s6rc_repo_syncset (char const *repo, size_t repolen, char const *set, stralloc *sa, genalloc *ga, unsigned int verbosity)
{
  for (unsigned int i = 0 ; i < 4 ; i++)
    if (unlink_stales_in_sub(repo, set, s6rc_repo_sublist[i], i, sa, ga, verbosity)) goto err ;

  {
    size_t n = genalloc_len(size_t, ga) ;
    char const *stillhere[n + !n] ;
    for (size_t i = 0 ; i < n ; i++) stillhere[i] = sa->s + genalloc_s(size_t, ga)[i] ;
    if (!s6rc_repo_fillset(repo, set, stillhere, n)) goto err ;
  }

  sa->len = 0 ;
  genalloc_setlen(size_t, ga, 0) ;
  return 1 ;

 err:
  sa->len = 0 ;
  genalloc_setlen(size_t, ga, 0) ;
  return 0 ;
}

int s6rc_repo_sync (char const *repo, char const *const *sources, size_t sourceslen, unsigned int verbosity, char const *fdhuser)
{
  size_t repolen = strlen(repo) ;
  char ato[repolen + 26] ;
  char bun[repolen + 26] ;


 /* Fill new .atomics/ and .bundles/ with symlinks to real sources */

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
        char const *x ;
        char dst[repolen + 28 + len] ;
        char src[srclen + len + 2] ;
        memcpy(src, sources[i], srclen) ;
        src[srclen] = '/' ;
        memcpy(src + srclen + 1, d->d_name, len+1) ;
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
      strerr_warnfu2sys("readdir ", sources[i]) ;
      goto err ;
    }
  }


 /* Compile the reference db - also checks new sources consistency */

  {
    char const *subs[2] = { ato + repolen + 9, bun + repolen + 9 } ;
    char oldc[repolen + 49] ;
    int r = s6rc_repo_compile(repo, ".ref", subs, 2, oldc, verbosity, fdhuser) ;
    if (r <= 0) goto err ;
    if (r == 2) rm_rf(oldc) ;
  }


 /* Switch to the new sources, delete the old ones */

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
      if (!s6rc_repo_syncset(repo, repolen, d->d_name, &sa, &ga, verbosity)) break ;
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
