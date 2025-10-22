/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/direntry.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

int s6rc_repo_ls (char const *fn, stralloc *sa, genalloc *ga)
{
  size_t sabase = sa->len ;
  size_t gabase = genalloc_len(size_t, ga) ;
  int sawasnull = !sa->s ;
  int gawasnull = !genalloc_s(size_t, ga) ;
  int n = 0 ;

  DIR *dir = opendir(fn) ;
  if (!dir) return -1 ;
  for (;;)
  {
    direntry *d ;
    size_t len ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    len = strlen(d->d_name) ;
    if (!genalloc_catb(size_t, ga, &sa->len, 1)) goto err ;
    if (!stralloc_catb(sa, d->d_name, len+1)) goto err ;
    n++ ;
  }
  if (errno) goto err ;
  dir_close(dir) ;
  return n ;

 err:
  dir_close(dir) ;
  if (gawasnull) genalloc_free(size_t, ga) ;
  else genalloc_setlen(size_t, ga, gabase) ;
  if (sawasnull) stralloc_free(sa) ;
  else sa->len = sabase ;
  return -1 ;
}
