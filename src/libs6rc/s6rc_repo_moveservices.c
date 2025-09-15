/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

int s6rc_repo_moveservices (char const *repo, char const *set, char const *const *services, size_t n, uint8_t oldsub, uint8_t newsub, unsigned int verbosity)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t maxserviceslen = 0 ;
  for (size_t i = 0 ; i < n ; i++)
  {
    size_t len = strlen(services[i]) ;
    if (len > maxserviceslen) maxserviceslen = len ;
  }
  char oldfn[repolen + setlen + maxserviceslen + 18] ;
  char newfn[repolen + setlen + maxserviceslen + 18] ;
  memcpy(oldfn, repo, repolen) ;
  memcpy(oldfn + repolen, "/sources/", 9) ;
  memcpy(oldfn + repolen + 9, set, setlen) ;
  oldfn[repolen + 9 + setlen] = '/' ;
  memcpy(oldfn + repolen + 10 + setlen, s6rc_repo_subnames[oldsub], 6) ;
  oldfn[repolen + 16 + setlen] = '/' ;
  memcpy(newfn, oldfn, repolen + 17 + setlen) ;
  memcpy(newfn + repolen + 10 + setlen, s6rc_repo_subnames[newsub], 6) ;

  for (size_t i = 0 ; i < n ; i++)
  {
    size_t len = strlen(services[i]) ;
    memcpy(oldfn + repolen + setlen + 17, services[i], len + 1) ;
    memcpy(newfn + repolen + setlen + 17, services[i], len + 1) ;
    if (access(newfn, F_OK) == 0)
    {
      if (verbosity >= 2)
        strerr_warni8x("service ", services[i], " already existed in subset ", s6rc_repo_subnames[newsub], " of set ", set, " in repository ", repo) ;
    }
    else if (errno != ENOENT)
    {
      strerr_warnfu2sys("access ", newfn) ;
      return 0 ;
    }
    if (rename(oldfn, newfn) == -1)
    {
      strerr_warnfu4sys("rename ", oldfn, " to ", newfn) ;
      return 0 ;
    }
    if (verbosity >= 3)
    {
      strerr_warnt10x("repository ", repo, ", set ", set, ", from subset ", s6rc_repo_subnames[oldsub], " to subset ", s6rc_repo_subnames[newsub], ": successfully moved service ", services[i]) ;
    }
  }
  return 1 ;
}
