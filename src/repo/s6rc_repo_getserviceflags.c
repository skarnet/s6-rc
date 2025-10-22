/* ISC license. */

#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/uint32.h>
#include <skalibs/types.h>
#include <skalibs/strerr.h>
#include <skalibs/cspawn.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

#include <skalibs/posixishard.h>

int s6rc_repo_getserviceflags (char const *repo, char const *name, uint32_t *flags)
{
  size_t repolen = strlen(repo) ;
  ssize_t r ;
  pid_t pid ;
  int fd ;
  int wstat ;
  char flagstr[10] ;
  char refdb[repolen + 15] ;
  char const *argv[8] = { S6RC_BINPREFIX "s6-rc-db", "-c", refdb, "-b", "--", "flags", name, 0 } ;

  memcpy(refdb, repo, repolen) ;
  memcpy(refdb + repolen, "/compiled/.ref", 15) ;
  pid = child_spawn1_pipe(argv[0], argv, (char const *const *)environ, &fd, 1) ;
  if (!pid) { strerr_warnfu2sys("spawn ", argv[0]) ; return -1 ; }
  r = readnclose(fd, flagstr, 10) ;
  if (r != 9)
  {
    if (r == 10) goto err ;
    else if (r >= 0) errno = EPIPE ;
    strerr_warnfu2sys("read output from ", argv[0]) ;
    return -1 ;
  }
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
    return (WEXITSTATUS(wstat) < 99) - 1 ;
  }
  flagstr[8] = 0 ;
  if (!uint320_xscan(flagstr, flags)) goto err ;
  return 1 ;

 err:
  strerr_warnf2x("invalid output from ", S6RC_BINPREFIX "s6-rc-db") ;
  errno = EPROTO ;
  return -1 ;
}
