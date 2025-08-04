/* ISC license. */

#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <skalibs/strerr.h>

#include <s6-rc/repo.h>

int s6rc_repo_set_moveservices (char const *repo, char const *set, char const *const *services, size_t n, char const *fromsub, char const *tosub)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t fromsublen = strlen(fromsub) ;
  size_t tosublen = strlen(tosub) ;
  size_t maxserviceslen = 0 ;
  size_t i = 0 ;
  for (; i < n ; i++)
  {
    size_t len = strlen(services[i]) ;
    if (len > maxserviceslen) maxserviceslen = len ;
  }
  char from[repolen + setlen + fromsublen + maxserviceslen + 12] ;
  char to[repolen + setlen + tosublen + maxserviceslen + 12] ;
  memcpy(from, repo, repolen) ;
  memcpy(from + repolen, "/sources/", 9) ;
  memcpy(from + repolen + 9, set, setlen) ;
  from[repolen + 9 + setlen] = '/' ;
  memcpy(to, from, repolen + 10 + setlen) ;
  memcpy(from + repolen + 10 + setlen, fromsub, fromsublen) ;
  from[repolen + 10 + setlen + fromsublen] = '/' ;
  memcpy(to + repolen + 10 + setlen, tosub, tosublen) ;
  to[repolen + 10 + setlen + tosublen] = '/' ;
  for (i = 0 ; i < n ; i++)
  {
    size_t len = strlen(services[i]) ;
    memcpy(from + repolen + setlen + fromsublen + 11, services[i], len + 1) ;
    memcpy(to + repolen + setlen + tosublen + 11, services[i], len + 1) ;
    if (rename(from, to) == -1) goto rollback ;
  }
  return 1 ;

 rollback:
  int e = errno ;
  strerr_warnfu4sys("rename ", from, " to ", to) ;
  while (i--)
  {
    size_t len = strlen(services[i]) ;
    memcpy(from + repolen + setlen + fromsublen + 11, services[i], len + 1) ;
    memcpy(to + repolen + setlen + tosublen + 11, services[i], len + 1) ;
    if (rename(to, from) == -1)
      strerr_warnwu4sys("rollback ", to, " to ", from) ;
  }
  errno = e ;
  return 0 ;
}
