/* ISC license. */

#include <skalibs/bsdsnowflake.h>  /* stat */
#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/stat.h>
#include <skalibs/direntry.h>
#include <skalibs/posixplz.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/repo.h>

static inline void cleanup (char const *fn)
{
  int e = errno ;
  rm_rf(fn) ;
  errno = e ;
}

int s6rc_repo_sync (char const *repo, char const *const *sources, size_t sourceslen)
{
  size_t repolen = strlen(repo) ;
  char newdir[repolen + 24] ;
  memcpy(newdir, repo, repolen) ;
  memcpy(newdir + repolen, "/.everything:cur:XXXXXX", 24) ;
  if (!mkdtemp(newdir)) return 0 ;

  for (size_t i = 0 ; i < sourceslen ; i++)
  {
    size_t srclen = strlen(sources[i]) ;
    DIR *dir = opendir(sources[i]) ;
    if (!dir) goto err ;
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
        memcpy(dst + repolen + 24, d->d_name, len + 1) ;
        memcpy(src, sources[i], srclen) ;
        src[srclen] = '/' ;
        memcpy(src + srclen + 1, d->d_name, len + 1) ;
        if (symlink(src, dst) == -1) break ;
      }
    }
    dir_close(dir) ;
    if (errno) goto err ;
  }

  if (chmod(newdir, 02755) == -1) goto err ;

  {
    char curdir[repolen + 13] ;
    memcpy(curdir, repo, repolen) ;
    memcpy(curdir + repolen, "/.everything", 13) ;
    if (!atomic_symlink(newdir + repolen + 1, curdir, "tmp")) goto err ;
  }

  return s6rc_repo_cleanup(repo) ;

 err:
  cleanup(newdir) ;
  return 0 ;
}
