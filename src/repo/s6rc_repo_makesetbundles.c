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

static int dosymlink (char const *dst, size_t dstlen, char const *name)
{
  size_t len = strlen(name) ;
  char target[16 + len] ;
  char fn[dstlen + len + 2] ;
  memcpy(target, "../../.bundles/", 15) ;
  memcpy(target + 15, name, len+1) ;
  memcpy(fn, dst, dstlen) ;
  fn[dstlen] = '/' ;
  memcpy(fn + dstlen + 1, name, len+1) ;
  if (symlink(target, fn) == -1)
  {
    strerr_warnfu4sys("symlink ", target, " to ", fn) ;
    return 0 ;
  }
  return 1 ;
}

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
  rm_rf(dst) ;
  if (mkdir(dst, 02755) == -1)
  {
    strerr_warnfu2sys("mkdir ", dst) ;
    return 0 ;
  }

  n = s6rc_repo_listrx(repo, set, s6rc_repo_rxnames[0], &sa, &ga) ;
  if (n < 0)
  {
    strerr_warnfu6sys("list rx ", s6rc_repo_rxnames[0], " of set ", set, " in repository ", repo) ;
    goto err ;
  }

  DIR *dir = opendir(src) ;
  if (!dir)
  {
    strerr_warnfu2sys("opendir ", src) ;
    goto err ;
  }

  if (n)
  {
    char const *masked[n] ;
    char maskedstorage[sa.len] ;
    memcpy(maskedstorage, sa.s, sa.len) ;
    for (size_t i = 0 ; i < n ; i++) masked[i] = maskedstorage + genalloc_s(size_t, &ga)[i] ;
    sa.len = 0 ;
    genalloc_setlen(size_t, &ga, 0) ;
    qsort(masked, n, sizeof(char const *), &str_cmp) ;

    for (;;)
    {
      int e ;
      unsigned int i = 0 ;
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;

      sa.len = 0 ;
      genalloc_setlen(size_t, &ga, 0) ;
      e = s6rc_repo_listcontents(repo, d->d_name, &sa, &ga) ;
      if (e) goto err0 ;

      for (; i < genalloc_len(size_t, &ga) ; i++)
      {
        char const *x = sa.s + genalloc_s(size_t, &ga)[i] ;
        if (bsearch(x, masked, n, sizeof(char const *), &str_bcmp))
        {
          if (verbosity >= 3)
            strerr_warni4x("skipping bundle ", d->d_name, " containing e.g. masked service ", x) ;
          break ;
        }
      }
      if (i < genalloc_len(size_t, &ga)) continue ;
      if (!dosymlink(dst, repolen + setlen + 16, d->d_name)) goto err0 ;
    }
  }
  else
  {
    for (;;)
    {
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;
      if (!dosymlink(dst, repolen + setlen + 16, d->d_name)) goto err0 ;
    }
  }

  if (errno)
  {
    strerr_warnfu2sys("readdir ", src) ;
    goto err0 ;
  }

  dir_close(dir) ;
  genalloc_free(size_t, &ga) ;
  stralloc_free(&sa) ;
  return 1 ;

 err0:
  dir_close(dir) ;
 err:
  {
    int e = errno ;
    rm_rf(dst) ;
    errno = e ;
  }
  genalloc_free(size_t, &ga) ;
  stralloc_free(&sa) ;
  return 0 ;
}
