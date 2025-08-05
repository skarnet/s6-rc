/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/types.h>
#include <skalibs/tai.h>
#include <skalibs/cspawn.h>
#include <skalibs/strerr.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

int s6rc_repo_compile (char const *repo, char const *set, char const *const *subs, size_t nsubs, char *oldc, unsigned int verbosity, char const *fdhuser)
{
  size_t repolen = strlen(repo) ;
  size_t setlen = strlen(set) ;
  size_t totsublen = 0 ;
  char newc[S6RC_REPO_COMPILE_BUFLEN(repolen, setlen)] ;
  memcpy(newc, repo, repolen) ;
  memcpy(newc + repolen, "/compiled/.", 11) ;
  memcpy(newc + repolen + 11, set, setlen) ;
  newc[repolen + 11 + setlen] = ':' ;
  timestamp_g(newc + repolen + 12 + setlen) ;
  memcpy(newc + repolen + 37 + setlen, ":XXXXXX", 8) ;
  memcpy(oldc, newc, repolen + 10) ;
  if (mkntemp(newc) == -1) { strerr_warnfu2sys("mkntemp ", newc) ; return -1 ; }
  for (size_t i = 0 ; i < nsubs ; i++) totsublen += strlen(subs[i]) + 1 ;

  {
    pid_t pid ;
    size_t m = 0 ;
    int wstat ;
    char const *argv[9 + nsubs + !nsubs] ;
    char fmtv[UINT_FMT] ;
    char src[(nsubs + !nsubs) * (repolen + 10 + setlen) + totsublen] ;
    char *w = src ;
    fmtv[uint_fmt(fmtv, verbosity)] = 0 ;
    argv[m++] = S6RC_BINPREFIX "s6-rc-compile" ;
    argv[m++] = "-v";
    argv[m++] = fmtv ;
    argv[m++] = "-b" ;
    if (fdhuser)
    {
      argv[m++] = "-h" ;
      argv[m++] = fdhuser ;
    }
    argv[m++] = "--" ;
    argv[m++] = newc ;
    if (nsubs)
    {
      for (size_t i = 0 ; i < nsubs ; i++)
      {
        size_t sublen = strlen(subs[i]) ;
        argv[m++] = w ;
        memcpy(w, repo, repolen) ; w += repolen ;
        memcpy(w, "/sources/", 9) ; w += 9 ;
        memcpy(w, set, setlen) ; w += setlen ;
        *w++ = '/' ;
        memcpy(w, subs[i], sublen+1) ; w += sublen + 1 ;
      }
    }
    else
    {
      argv[m++] = w ;
      memcpy(w, repo, repolen) ; w += repolen ;
      memcpy(w, "/sources/", 9) ; w += 9 ;
      memcpy(w, set, setlen+1) ; w += setlen+1 ;
    }
    argv[m++] = 0 ;
    pid = cspawn(argv[0], argv, (char const *const *)environ, 0, 0, 0) ;
    if (!pid) { strerr_warnfu2sys("spawn ", argv[0]) ; return -1 ; }
    if (wait_pid(pid, &wstat) == -1)
    {
      strerr_warnfu2sys("wait for ", argv[0]) ;
      return -1 ;
    }
    if (WIFSIGNALED(wstat))
    {
      char fmt[UINT_FMT] ;
      fmt[uint_fmt(fmt, WTERMSIG(wstat))] = 0 ;
      strerr_warnf3x(argv[0], " crashed with signal ", fmt) ;
      return -1 ;
    }
    if (WEXITSTATUS(wstat))
    {
      char fmt[UINT_FMT] ;
      fmt[uint_fmt(fmt, WEXITSTATUS(wstat))] = 0 ;
      strerr_warnf3x(argv[0], " exited with code ", fmt) ;
      return (WEXITSTATUS(wstat) == 1) - 1 ;
    }
  }
  {
    char fn[repolen + setlen + 11] ;
    memcpy(fn, newc, repolen + 10) ;
    memcpy(fn + repolen + 10, set, setlen + 1) ;
    if (!atomic_symlink4(newc + repolen + 10, fn, oldc + repolen + 10, repolen + setlen + 45))
    {
      int e = errno ;
      rm_rf(newc) ;
      errno = e ;
      strerr_warnfu4sys("atomically make a ", fn, " symlink pointing to ", newc + repolen + 10) ;
      return -1 ;
    }
  }
  return 1 + !!oldc[repolen + 10] ;
}
