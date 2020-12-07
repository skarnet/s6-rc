/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <skalibs/posixplz.h>
#include <skalibs/types.h>
#include <skalibs/sgetopt.h>
#include <skalibs/strerr2.h>
#include <skalibs/stralloc.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-init [ -c compiled ] [ -l live ] [ -p prefix ] [ -t timeout ] [ -d ] scandir"
#define dieusage() strerr_dieusage(100, USAGE)

static void cleanup (stralloc *sa)
{
  int e = errno ;
  rm_rf_in_tmp(sa, 0) ;
  errno = e ;
}

int main (int argc, char const *const *argv)
{
  tain_t deadline ;
  stralloc sa = STRALLOC_ZERO ;
  size_t dirlen ;
  char const *live = S6RC_LIVE_BASE ;
  char const *compiled = S6RC_COMPILED_BASE ;
  char const *prefix = "" ;
  int deref = 0 ;
  PROG = "s6-rc-init" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "c:l:p:t:bd", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'c' : compiled = l.arg ; break ;
        case 'l' : live = l.arg ; break ;
        case 'p' : prefix = l.arg ; break ;
        case 't' : if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        case 'b' : break ;
        case 'd' : deref = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&deadline, t) ;
    else deadline = tain_infinite_relative ;
  }
  if (!argc) dieusage() ;

  if (!deref && compiled[0] != '/')
    strerr_dief2x(100, compiled, " is not an absolute path") ;
  if (live[0] != '/')
    strerr_dief2x(100, live, " is not an absolute path") ;
  if (argv[0][0] != '/')
    strerr_dief2x(100, argv[0], " is not an absolute path") ;
  if (strchr(prefix, '/') || strchr(prefix, '\n'))
    strerr_dief1x(100, "prefix cannot contain a / or a newline") ;

  tain_now_set_stopwatch_g() ;
  tain_add_g(&deadline, &deadline) ;

  if (deref)
  {
    char *x = realpath(compiled, 0) ;
    if (!x) strerr_diefu2sys(111, "realpath ", compiled) ;
    compiled = x ;
  }
  {
    s6rc_db_t db ;
    int r ;
    int fdcompiled = open_readb(compiled) ;
    if (fdcompiled < 0)
      strerr_diefu2sys(111, "open ", compiled) ;
    r = s6rc_db_read_sizes(fdcompiled, &db) ;
    if (r < 0)
      strerr_diefu2sys(111, "read database size in ", compiled) ;
    else if (!r)
      strerr_dief2x(4, "invalid database size in ", compiled) ;
    close(fdcompiled) ;
    {
      unsigned char state[db.nshort + db.nlong] ;
      memset(state, 0, db.nshort + db.nlong) ;
      if (!s6rc_livedir_create(&sa, live, PROG, argv[0], prefix, compiled, state, db.nshort + db.nlong, &dirlen))
        strerr_diefu1sys(111, "create live directory") ;
    }
  }
  {
    size_t clen = strlen(compiled) ;
    char lfn[sa.len + 13] ;
    char cfn[clen + 13] ;
    memcpy(lfn, sa.s, sa.len) ;
    memcpy(lfn + sa.len, "/servicedirs", 13) ;
    memcpy(cfn, compiled, clen) ;
    memcpy(cfn + clen, "/servicedirs", 13) ;
    sa.len++ ;
    if (!hiercopy(cfn, lfn))
    {
      cleanup(&sa) ;
      strerr_diefu4sys(111, "recursively copy ", cfn, " to ", lfn) ;
    }
  }
  if (!atomic_symlink(sa.s + dirlen, live, PROG))
  {
    cleanup(&sa) ;
    strerr_diefu4sys(111, "symlink ", sa.s + dirlen, " to ", live) ;
  }

  if (s6rc_servicedir_manage_g(live, prefix, &deadline) < 0)
  {
    unlink_void(live) ;
    cleanup(&sa) ;
    if (errno == ENXIO)
      strerr_diefu5x(100, "supervise service directories in ", live, "/servicedirs", ": s6-svscan not running on ", argv[0]) ;
    else
      strerr_diefu3sys(111, "supervise service directories in ", live, "/servicedirs") ;
  }
  return 0 ;
}
