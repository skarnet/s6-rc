/* ISC license. */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/direntry.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

static int str_ref_cmp (void const *a, void const *b)
{
  return strcmp(*(char const *const *)a, *(char const *const *)b) ;
}


int s6rc_repo_makebbuild (char const *repo, char const *set)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  stralloc sa = STRALLOC_ZERO ;
  genalloc ga = GENALLOC_ZERO ;
  int n ;
  char src[repolen + 17 + setlen] ;
  char dst[repolen + 17 + setlen] ;
  memcpy(dst, repo, repolen) ;
  memcpy(dst + repolen, "/sources/", 9) ;
  memcpy(dst + repolen + 9, set, setlen) ;
  memcpy(dst + repolen + 9 + setlen, "/bbuild", 8) ;
  memcpy(src, dst, repolen + 10 + setlen) ;
  memcpy(src + repolen + 10 + setlen, "bundle") ;
  rmstar_tmp(dst, &sa) ;

  n = s6rc_repo_listsub(repo, set, "masked", &sa, &ga) ;
  if (n < 0) goto err ;
  if (n)
  {
    DIR *dir ;
    char const *masked[n] ;
    for (size_t i = 0 ; i < n ; i++) masked[i] = sa.s + genalloc_s(size_t, &ga)[i] ;
    genalloc_free(size_t, &ga) ;
    qsort(masked, n, sizeof(char const *), &str_ref_cmp) ;
    dir = opendir(src) ;
    if (!dir) goto err ;
    for (;;)
    {
      direntry *d ;
      errno = 0 ;
      d = readdir(dir) ;
      if (!d) break ;
      if (d->d_name[0] == '.') continue ;
      if (bsearch(&d->d_name, masked, n, sizeof(char const *), &str_ref_cmp)) continue ;
      size_t len = strlen(d->d_name) ;
      char target[16 + len] ;
      char fn[repolen + setlen + 18 + len] ;
      memcpy(target, "../.everything/", 15) ;
      memcpy(target + 15, d->d_name, len+1) ;
      memcpy(fn, dst, repolen + setlen + 17) ;
      memcpy(fn + repolen + setlen + 17, d->d_name, len+1) ;
      if (symlink(target, fn) == -1)
      {
        strerr_warnfu4sys("make a symlink named ", fn, " pointing to ", target) ;
        goto err0 ;
    }
    dir_close(dir) ;
    if (errno) goto err0 ;
  }
  else if (!hiercopy_tmp(src, dst, &sa)) goto err ;

  stralloc_free(&sa) ;
  return 1 ;

 err0:
  {
    int e = errno ;
    rmstar_tmp(dst, &sa) ;
    errno = e ;
  }
 err:
  stralloc_free(&sa) ;
  return 0 ;
}
