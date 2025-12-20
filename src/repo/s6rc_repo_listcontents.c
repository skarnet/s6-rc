/* ISC license. */

#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/types.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/cspawn.h>
#include <skalibs/djbunix.h>
#include <skalibs/skamisc.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

int s6rc_repo_listcontents (char const *repo, char const *bundle, stralloc *storage, genalloc *indices)
{
  size_t sabase = storage->len ;
  size_t gabase = genalloc_len(size_t, indices) ;
  size_t repolen = strlen(repo) ;
  pid_t pid ;
  int fd ;
  int wstat ;
  char refdb[repolen + 15] ;
  char const *argv[8] = { S6RC_BINPREFIX "s6-rc-db", "-c", refdb, "-b", "--", "contents", bundle, 0 } ;

  memcpy(refdb, repo, repolen) ;
  memcpy(refdb + repolen, "/compiled/.ref", 15) ;
  pid = child_spawn1_pipe(argv[0], argv, (char const *const *)environ, &fd, 1) ;
  if (!pid) { strerr_warnfu2sys("spawn ", argv[0]) ; return 111 ; }
  if (!slurpn(fd, storage, 0)) { strerr_warnfu2sys("read output from ", argv[0]) ; return 111 ; }
  fd_close(fd) ;
  if (wait_pid(pid, &wstat) == -1) { strerr_warnfu2sys("wait for ", argv[0]) ; return 111 ; }
  if (WIFSIGNALED(wstat))
  {
    char fmt[UINT_FMT] ;
    fmt[uint_fmt(fmt, WTERMSIG(wstat))] = 0 ;
     strerr_warnf3x(argv[0], " crashed with signal ", fmt) ;
    return wait_estatus(wstat) ;
  }
  if (WEXITSTATUS(wstat))
  {
    char fmt[UINT_FMT] ;
    fmt[uint_fmt(fmt, WEXITSTATUS(wstat))] = 0 ;
    strerr_warnf3x(argv[0], " exited with code ", fmt) ;
    return WEXITSTATUS(wstat) ;
  }

  if (!string_index(storage->s, sabase, storage->len - sabase, '\n', indices))
  {
    strerr_warnfu1sys("index result from s6-rc-db") ;
    goto err ;
  }
  s6rc_repo_removeinternals(indices, gabase, storage->s) ;
  return 0 ;

 err:
  genalloc_setlen(size_t, indices, gabase) ;
  storage->len = sabase ;
  return 111 ;
}
