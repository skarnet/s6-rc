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

#include <s6-rc/config.h>
#include <s6-rc/s6rc-utils.h>
#include <s6-rc/repo.h>

int s6rc_repo_listdeps_internal (char const *repo, char const *const *services, size_t n, stralloc *storage, genalloc *indices, uint32_t options)
{
  int swasnull = !storage->s ;
  int gwasnull = !indices->s ;
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
    for (size_t i = 0 ; i < n ; i++) argv[m++] = services[i] ;
    argv[m++] = 0 ;
    pid = child_spawn1_pipe(argv[0], argv, (char const *const *)environ, &fd, 1) ;
    if (!pid) { strerr_warnfu2sys("spawn ", argv[0]) ; return -1 ; }
    if (!slurpn(fd, storage, 0)) { strerr_warnfu2sys("read output from ", argv[0]) ; return -1 ; }
    fd_close(fd) ;
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
  }

 if (!s6rc_nlto0(storage->s + sabase, sabase, storage->len, indices)) goto err ;
 return 1 ;

 err:
  if (gwasnull) genalloc_free(size_t, indices) ;
  else genalloc_setlen(size_t, indices, gabase) ;
  if (swasnull) stralloc_free(storage) ;
  else storage->len = sabase ;
  return -1 ;
}
