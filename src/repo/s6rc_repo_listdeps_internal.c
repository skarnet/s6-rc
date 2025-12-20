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

int s6rc_repo_listdeps_internal (char const *repo, char const *const *services, uint32_t n, stralloc *storage, genalloc *indices, uint32_t options)
{
  size_t sabase = storage->len ;
  size_t gabase = genalloc_len(size_t, indices) ;

  {
    size_t m = 0 ;
    size_t repolen = strlen(repo) ;
    pid_t pid ;
    int fd ;
    int wstat ;
    char const *argv[8 + n] ;
    char refdb[repolen + 15] ;
    memcpy(refdb, repo, repolen) ;
    memcpy(refdb + repolen, "/compiled/.ref", 15) ;
    argv[m++] = S6RC_BINPREFIX "s6-rc-db" ;
    argv[m++] = "-c";
    argv[m++] = refdb ;
    argv[m++] = "-b" ;
    argv[m++] = options & 1 ? "-u" : "-d" ;
    argv[m++] = "--" ;
    argv[m++] = options & 2 ? "all-dependencies" : "dependencies" ;
    for (uint32_t i = 0 ; i < n ; i++) argv[m++] = services[i] ;
    argv[m++] = 0 ;
    pid = child_spawn1_pipe(argv[0], argv, (char const *const *)environ, &fd, 1) ;
    if (!pid) { strerr_warnfu2sys("spawn ", argv[0]) ; return 111 ; }
    if (!slurpn(fd, storage, 0)) { strerr_warnfu2sys("read output from ", argv[0]) ; return 111 ; }
    fd_close(fd) ;
    if (wait_pid(pid, &wstat) == -1)
    {
      strerr_warnfu2sys("wait for ", argv[0]) ;
      goto err ;
    }
    if (WIFSIGNALED(wstat))
    {
      char fmt[UINT_FMT] ;
      fmt[uint_fmt(fmt, WTERMSIG(wstat))] = 0 ;
      strerr_warnf3x(argv[0], " crashed with signal ", fmt) ;
      goto err ;
    }
    if (WEXITSTATUS(wstat)) return wait_estatus(wstat) ;
  }

 if (!string_index(storage->s, sabase, storage->len - sabase, '\n', indices)) goto err ;
 s6rc_repo_removeinternals(indices, gabase, storage->s) ;
 return 0 ;

 err:
  genalloc_setlen(size_t, indices, gabase) ;
  storage->len = sabase ;
  return 111 ;
}
