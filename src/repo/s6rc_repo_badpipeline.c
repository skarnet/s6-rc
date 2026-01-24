/* ISC license. */

#include <sys/wait.h>
#include <string.h>
#include <unistd.h>

#include <skalibs/posixplz.h>
#include <skalibs/types.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/cspawn.h>
#include <skalibs/djbunix.h>

#include <s6-rc/config.h>
#include <s6-rc/repo.h>

static int notfound (uint32_t k, uint32_t const *tab, uint32_t n)
{
  for (uint32_t i = 0 ; i < n ; i++) if (k == tab[i]) return 0 ;
  return 1 ;
}

int s6rc_repo_badpipeline (char const *repo, char const *set, size_t svind, s6rc_repo_sv const *svlist, uint32_t ntot, uint8_t newrx, stralloc *sa, genalloc *badga)
{
  size_t sabase = sa->len ;
  size_t gabase = genalloc_len(uint32_t, badga) ;
  size_t repolen = strlen(repo) ;
  pid_t pid ;
  int fd ;
  int wstat ;
  size_t mark = sabase ;
  char refdb[repolen + 15] ;
  char const *argv[8] = { S6RC_BINPREFIX "s6-rc-db", "-c", refdb, "-b", "--", "pipeline", sa->s + svind, 0 } ;
  memcpy(refdb, repo, repolen) ;
  memcpy(refdb + repolen, "/compiled/.ref", 15) ;
  pid = child_spawn1_pipe(argv[0], argv, (char const *const *)environ, &fd, 1) ;
  if (!pid) { strerr_warnfu2sys("spawn ", argv[0]) ; return 111 ; }
  if (!slurpn(fd, sa, 0)) { strerr_warnfu2sys("read output from ", argv[0]) ; return 111 ; }
  fd_close(fd) ;
  if (wait_pid(pid, &wstat) == -1)
  {
    strerr_warnfu2sys("wait for ", argv[0]) ;
    return 111 ;
  }
  if (WIFSIGNALED(wstat))
  {
    char fmt[UINT_FMT] ;
    fmt[uint_fmt(fmt, WTERMSIG(wstat))] = 0 ;
     strerr_warnf3x(argv[0], " crashed with signal ", fmt) ;
    return wait_estatus(wstat) ;
  }
  if (WEXITSTATUS(wstat) == 5) return 0 ;
  if (WEXITSTATUS(wstat))
  {
    char fmt[UINT_FMT] ;
    fmt[uint_fmt(fmt, WEXITSTATUS(wstat))] = 0 ;
    strerr_warnf3x(argv[0], " exited with code ", fmt) ;
    return WEXITSTATUS(wstat) ;
  }

  for (size_t pos = sabase ; pos < sa->len ; pos++)
  {
    if (sa->s[pos] == '/' || sa->s[pos] == '\n')
    {
      sa->s[pos] = 0 ;
      if (strcmp(sa->s + mark, sa->s + svind))
      {
        uint32_t k ;
        s6rc_repo_sv *p = bsearchr(sa->s + mark, svlist, ntot, sizeof(s6rc_repo_sv), &s6rc_repo_sv_bcmpr, sa->s) ;
        if (!p)
        {
          strerr_warnfu("find service ", sa->s + mark, " in set ", set, " of repository ", repo) ;
          goto err ;
        }
        k = p - svlist ;
        if (!newrx != !p->rx && notfound(k, genalloc_s(uint32_t, badga), genalloc_len(uint32_t, badga)))
        {
          if (!genalloc_append(uint32_t, badga, &k))
          {
            strerr_warnfu1sys("make bad pipeline list") ;
            goto err ;
          }
        }
      }
      mark = pos + 1 ;
    }
  }
  sa->len = sabase ;
  return 0 ;

 err:
  sa->len = sabase ;
  genalloc_setlen(uint32_t, badga, gabase) ;
  return 111 ;
}
