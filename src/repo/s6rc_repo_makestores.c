/* ISC license. */

#include <skalibs/nonposix.h>  /* mkdtemp */

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/uint16.h>
#include <skalibs/posixplz.h>
#include <skalibs/stat.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/repo.h>

int s6rc_repo_makestores (char const *repo, char const *const *stores, uint16_t n, char *sold)
{
  size_t repolen = strlen(repo) ;
  mode_t m = umask(0) ;
  char slink[repolen + 8] ;
  char sdir[repolen + 21] ;

  memcpy(slink, repo, repolen) ;
  memcpy(slink + repolen, "/stores", 8) ;
  memcpy(sdir, slink, repolen + 1) ;
  memcpy(sdir + repolen + 1, ".stores:XXXXXX", 15) ;

  if (!mkdtemp(sdir))
  {
    umask(m) ;
    strerr_warnfu2sys("mkdtemp ", sdir) ;
    return 0 ;
  }
  umask(m) ;
  sdir[repolen + 15] = '/' ;
  sdir[repolen + 20] = 0 ;
  for (uint16_t i = 0 ; i < n ; i++)
  {
    uint160_xfmt(sdir + repolen + 16, i, 4) ;
    if (symlink(stores[i], sdir) == -1)
    {
      strerr_warnfu4sys("symlink ", stores[i], " to ", sdir) ;
      sdir[repolen + 15] = 0 ;
      goto err ;
    }
  }
  sdir[repolen + 15] = 0 ;
  if (chmod(sdir, 02755) == -1)
  {
    strerr_warnfu2sys("chmod ", sdir) ;
    goto err ;
  }
  if (!atomic_symlink4(sdir + repolen + 1, slink, sold, 15))
  {
    strerr_warnfu4sys("atomically symlink ", sdir + repolen + 1, " to ", slink) ;
    goto err ;
  }
  return 1 ;

 err:
  rm_rf(sdir) ;
  return 0 ;
}
