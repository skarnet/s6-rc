/* ISC license. */

#include <skalibs/bsdsnowflake.h>

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/direntry.h>
#include <skalibs/bytestr.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

int s6rc_repo_makesetbundles (char const *repo, char const *set, unsigned int verbosity)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  stralloc sa = STRALLOC_ZERO ;
  genalloc ga = GENALLOC_ZERO ;
  int n ;
  char src[repolen + 18] ;
  char dst[repolen + 17 + setlen] ;
  memcpy(dst, repo, repolen) ;
  memcpy(dst + repolen, "/sources/", 9) ;
  memcpy(dst + repolen + 9, set, setlen) ;
  memcpy(dst + repolen + 9 + setlen, "/bundle", 8) ;
  memcpy(src, dst, repolen + 9) ;
  memcpy(src + repolen + 9, ".bundles", 9) ;
  rm_rf_tmp(dst, &sa) ;

  n = s6rc_repo_listsub(repo, set, s6rc_repo_subnames[0], &sa, &ga) ;
  if (n < 0)
  {
    strerr_warnfu6sys("list sub ",  s6rc_repo_subnames[0], " of set ", set, " in repository ", repo) ;
    goto err ;
  }
  if (n)
  {
    DIR *dir ;
    char const *masked[n] ;
    char maskedstorage[sa.len] ;
    memcpy(maskedstorage, sa.s, sa.len) ;
    for (size_t i = 0 ; i < n ; i++) masked[i] = maskedstorage + genalloc_s(size_t, &ga)[i] ;
    sa.len = 0 ;
    genalloc_setlen(size_t, &ga, 0) ;
    qsort(masked, n, sizeof(char const *), &str_cmp) ;

    if (mkdir(dst, 02755) == -1)
    {
      strerr_warnfu2sys("mkdir ", dst) ;
      goto errga ;
    }
    dir = opendir(src) ;
    if (!dir)
    {
      strerr_warnfu2sys("opendir ", src) ;
      goto err0 ;
    }
    for (;;)
    {
      int m, i = 0 ;
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;

      sa.len = 0 ;
      genalloc_setlen(size_t, &ga, 0) ;
      m = s6rc_repo_listcontents(repo, d->d_name, &sa, &ga) ;
      if (m < 0)
      {
        dir_close(dir) ;
        goto err0 ;
      }

      for (; i < m ; i++)
      {
        char const *x = sa.s + genalloc_s(size_t, &ga)[i] ;
        if (bsearch(x, masked, n, sizeof(char const *), &str_bcmp))
        {
          if (verbosity >= 3)
            strerr_warni4x("skipping bundle ", d->d_name, " containing e.g. masked service ", x) ;
          break ;
        }
      }
      if (i < m) continue ;

      size_t len = strlen(d->d_name) ;
      char target[16 + len] ;
      char fn[repolen + setlen + 18 + len] ;
      memcpy(target, "../../.bundles/", 15) ;
      memcpy(target + 15, d->d_name, len+1) ;
      memcpy(fn, dst, repolen + setlen + 16) ;
      fn[repolen + setlen + 16] = '/' ;
      memcpy(fn + repolen + setlen + 17, d->d_name, len+1) ;
      if (symlink(target, fn) == -1)
      {
        strerr_warnfu4sys("symlink ", target, " to ", fn) ;
        dir_close(dir) ;
        goto err0 ;
      }
    }
    dir_close(dir) ;
    if (errno)
    {
      strerr_warnfu2sys("readdir ", src) ;
      goto err0 ;
    }
  }
  else
  {
    int r ;
    char realsrc[repolen + 28] ;
    memcpy(realsrc, src, repolen + 9) ;
    r = readlink(src, realsrc + repolen + 9, 17) ;
    if (r == -1)
    {
      strerr_warnfu2sys("readlink ", src) ;
      goto err0 ;
    }
    if (r != 16)
    {
      strerr_warnf2x("invalid symlink for ", src) ;
      goto err0 ;
    }
    if (!hiercopy_tmp(realsrc, dst, &sa))
    {
      strerr_warnfu4sys("recursively copy ", realsrc, " to ", dst) ;
      goto err0 ;
    }
  }

  stralloc_free(&sa) ;
  return 1 ;

 err0:
  {
    int e = errno ;
    rm_rf_tmp(dst, &sa) ;
    errno = e ;
  }
 errga:
  genalloc_free(size_t, &ga) ;
 err:
  stralloc_free(&sa) ;
  return 0 ;
}
