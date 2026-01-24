/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/djbunix.h>

#include <s6-rc/repo.h>

int s6rc_repo_setcompile (char const *repo, char const *set, char const *defbundle, char *oldc, unsigned int verbosity, char const *fdhuser)
{
  static char const *const rxs[4] = { "bundle", s6rc_repo_rxnames[1], s6rc_repo_rxnames[2], s6rc_repo_rxnames[3] } ;
  int r ;
  if (!s6rc_repo_makesetbundles(repo, set, verbosity)) return -1 ;
  if (!s6rc_repo_makedefbundle(repo, set, defbundle)) return -1 ;
  r = s6rc_repo_compile(repo, set, rxs, 4, oldc, verbosity, fdhuser) ;

  {
    int e = errno ;
    size_t repolen = strlen(repo) ;
    size_t setlen = strlen(set) ;
    size_t rxlen = strlen(rxs[0]) ;
    char fn[repolen + setlen + 11 + rxlen] ;
    memcpy(fn, repo, repolen) ;
    memcpy(fn + repolen, "/sources/", 9) ;
    memcpy(fn + repolen + 9, set, setlen) ;
    fn[repolen + 9 + setlen] = '/' ;
    memcpy(fn + repolen + setlen + 10, rxs[0], rxlen + 1) ;
    rm_rf(fn) ;
    errno = e ;
  }
  return r ;
}
