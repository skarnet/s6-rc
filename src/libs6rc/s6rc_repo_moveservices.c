/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

int s6rc_repo_moveservices (char const *repo, char const *set, s6rc_repo_sv const *services, uint32_t n, uint8_t newsub, char const *storage, unsigned int verbosity)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t maxserviceslen = 0 ;
  for (uint32_t i = 0 ; i < n ; i++)
  {
    size_t len = strlen(storage + services[i].pos) ;
    if (len > maxserviceslen) maxserviceslen = len ;
  }
  char oldfn[repolen + setlen + maxserviceslen + 18] ;
  char newfn[repolen + setlen + maxserviceslen + 18] ;
  memcpy(oldfn, repo, repolen) ;
  memcpy(oldfn + repolen, "/sources/", 9) ;
  memcpy(oldfn + repolen + 9, set, setlen) ;
  oldfn[repolen + 9 + setlen] = '/' ;
  oldfn[repolen + 16 + setlen] = '/' ;
  memcpy(newfn, oldfn, repolen + 10 + setlen) ;
  memcpy(newfn + repolen + 10 + setlen, s6rc_repo_subnames[newsub], 6) ;
  newfn[repolen + 16 + setlen] = '/' ;

  for (uint32_t i = 0 ; i < n ; i++)
  {
    size_t len = strlen(storage + services[i].pos) ;
    memcpy(oldfn + repolen + 10, s6rc_repo_subnames[services[i].sub], 6) ;
    memcpy(oldfn + repolen + setlen + 17, storage + services[i].pos, len + 1) ;
    memcpy(newfn + repolen + setlen + 17, storage + services[i].pos, len + 1) ;
    if (access(newfn, F_OK) == 0)
    {
      if (verbosity >= 2)
        strerr_warni8x("service ", storage + services[i].pos, " already existed in subset ", s6rc_repo_subnames[newsub], " of set ", set, " in repository ", repo) ;
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
    if (verbosity >= 4)
    {
      strerr_warnt10x("repository ", repo, ", set ", set, ", from subset ", s6rc_repo_subnames[services[i].sub], " to subset ", s6rc_repo_subnames[newsub], ": successfully moved service ", storage + services[i].pos) ;
    }
  }
  return 1 ;
}
