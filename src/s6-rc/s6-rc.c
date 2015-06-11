/* ISC license. */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <skalibs/uint32.h>
#include <skalibs/uint.h>
#include <skalibs/bytestr.h>
#include <skalibs/cdb.h>
#include <skalibs/sgetopt.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/tai.h>
#include <skalibs/environ.h>
#include <skalibs/djbunix.h>
#include <skalibs/selfpipe.h>
#include <skalibs/iopause.h>
#include <skalibs/unix-transactional.h>
#include <s6/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc [ -v verbosity ] [ -n dryruntimeout ] [ -t timeout ] [ -l live ] [ -u | -d ] [ -p ] [ -a ] [ -C ] [ -L ] [ -A ] [ -S ] servicenames..."
#define dieusage() strerr_dieusage(100, USAGE)

typedef struct pidindex_s pidindex_t ;
struct pidindex_s
{
  pid_t pid ;
  unsigned int i ;
} ;

typedef struct what_s what_t ;
struct what_s
{
  unsigned char up : 1 ;
  unsigned char prune : 1 ;
  unsigned char selectlive : 1 ;
  unsigned char check : 1 ;
  unsigned char list : 1 ;
  unsigned char listall : 1 ;
  unsigned char change : 1 ;
} ;

static unsigned int verbosity = 1 ;
static char const *live = S6RC_LIVE_BASE ;
static unsigned int livelen ;
static pidindex_t *pidindex ;
static unsigned int npids = 0 ;
static s6rc_db_t *db ;
static unsigned int n ;
static unsigned char *state ;
static unsigned int *pendingdeps ;
static tain_t deadline ;
static char dryrun[UINT_FMT] = "" ;

static inline void announce (void)
{
  unsigned int i = n ;
  char fn[livelen + 7] ;
  char tmpstate[i] ;
  if (dryrun[0]) return ;
  byte_copy(fn, livelen, live) ;
  byte_copy(fn + livelen, 7, "/state") ;
  while (i--) tmpstate[i] = !!(state[i] & 1) ;
  if (!openwritenclose_suffix(fn, tmpstate, n, ".new"))
    strerr_diefu2sys(111, "write ", fn) ;
}

static void print_services (void)
{
  register unsigned int i = 0 ;
  for (; i < n ; i++)
    if (state[i] & 2)
    {
      buffer_puts(buffer_1, db->string + db->services[i].name) ;
      buffer_put(buffer_1, "\n", 1) ;
    }
  buffer_putflush(buffer_1, "\n", 1) ;
}

static pid_t start_oneshot (unsigned int i, int h)
{
  unsigned int argc = db->services[i].x.oneshot.argc[h] ;
  char const *const *argv = (char const *const *)db->argvs + h * db->ndeps + db->services[i].x.oneshot.argv[h] ;
  unsigned int m = 0 ;
  char const *newargv[9 + argc + (!!dryrun[0] << 2)] ;
  char fmt[UINT32_FMT] ;
  char socketfn[livelen + S6RC_ONESHOT_RUNNER_LEN + 3] ;
  byte_copy(socketfn, livelen, live) ;
  byte_copy(socketfn + livelen, 16 + S6RC_ONESHOT_RUNNER_LEN, "/servicedirs/" S6RC_ONESHOT_RUNNER "/s") ;
  fmt[uint32_fmt(fmt, db->services[i].timeout[h])] = 0 ;
  if (dryrun[0])
  {
    newargv[m++] = S6_BINPREFIX "s6-rc-dryrun" ;
    newargv[m++] = "-t" ;
    newargv[m++] = dryrun ;
    newargv[m++] = "--" ;
  }
  newargv[m++] = S6_BINPREFIX "s6-sudo" ;
  newargv[m++] = verbosity >= 2 ? "-vel0" : "-el0" ;
  newargv[m++] = "-t" ;
  newargv[m++] = "2000" ;
  newargv[m++] = "-T" ;
  newargv[m++] = fmt ;
  newargv[m++] = "--" ;
  newargv[m++] = socketfn ;
  while (argc--) newargv[m++] = *argv++ ;
  newargv[m++] = 0 ;
  return child_spawn0(newargv[0], newargv, (char const *const *)environ) ;
}

static pid_t start_longrun (unsigned int i, int h)
{
  unsigned int namelen = str_len(db->string + db->services[i].name) ;
  unsigned int m = 0 ;
  char fmt[UINT32_FMT] ;
  char servicefn[livelen + namelen + 19] ;
  char const *newargv[11 + (!!dryrun[0] << 2)] ;
  byte_copy(servicefn, livelen, live) ;
  byte_copy(servicefn + livelen, 13, "/servicedirs/") ;
  byte_copy(servicefn + livelen + 13, namelen, db->string + db->services[i].name) ;
  byte_copy(servicefn + livelen + 13 + namelen, 6, "/down") ;
  fmt[uint32_fmt(fmt, db->services[i].timeout[h])] = 0 ;  
  if (dryrun[0])
  {
    newargv[m++] = S6_BINPREFIX "s6-rc-dryrun" ;
    newargv[m++] = "-t" ;
    newargv[m++] = dryrun ;
    newargv[m++] = "--" ;
  }
  newargv[m++] = S6_BINPREFIX "s6-svlisten1" ;
  newargv[m++] = h ? "-U" : "-d" ;
  newargv[m++] = "-t" ;
  newargv[m++] = fmt ;
  newargv[m++] = "--" ;
  newargv[m++] = servicefn ;
  newargv[m++] = S6_BINPREFIX "s6-svc" ;
  newargv[m++] = h ? "-u" : "-d" ;
  newargv[m++] = "--" ;
  newargv[m++] = servicefn ;
  newargv[m++] = 0 ;

  if (!dryrun[0])
  {
    if (h)
    {
      if (unlink(servicefn) < 0 && verbosity)
        strerr_warnwu2sys("unlink ", servicefn) ;
    }
    else
    {
      int fd = open_trunc(servicefn) ;
      if (fd < 0)
      {
        if (verbosity)
          strerr_warnwu2sys("touch ", servicefn) ;
      }
      else fd_close(fd) ;
    }
  }
  servicefn[livelen + 13 + namelen] = 0 ;
  return child_spawn0(newargv[0], newargv, (char const *const *)environ) ;
}

static void broadcast_success (unsigned int, int) ;

static void examine (unsigned int i, int h)
{
  char const *name = db->string + db->services[i].name ;
  if (verbosity >= 3)
    strerr_warni2x("examining ", name) ;

  if (!pendingdeps[i] && !(state[i] & 4))
  {
    state[i] |= 4 ;
    if ((state[i] & 1) == h)
    {
      if (verbosity >= 2)
        strerr_warni4x("processing service ", name, ": already ", h ? "up" : "down") ;
      broadcast_success(i, h) ;
    }
    else
    {
      pidindex[npids].pid = db->services[i].type ? start_longrun(i, h) : start_oneshot(i, h) ;
      if (pidindex[npids].pid)
      {
        pidindex[npids++].i = i ;
        if (verbosity >= 2)
        {
          strerr_warni4x("processing service ", name, ": ", h ? "starting" : "stopping") ;
        }
      }
      else
      {
        if (verbosity)
          strerr_warnwu2sys("spawn subprocess for ", name) ;
        if (verbosity >= 2)
          strerr_warni5x("processing service ", name, ": ", h ? "start" : "stop", " failed") ;
      }
    }
  }
}

static void broadcast_success (unsigned int i, int h)
{
  unsigned int nd = db->services[i].ndeps[!h] ;
  while (nd--)
  {
    unsigned int j = db->deps[!h * db->ndeps + db->services[i].deps[!h] + nd] ;
    pendingdeps[j]-- ;
    examine(j, h) ;
  }
}

static void on_success (unsigned int i, int h)
{
  if (h) state[i] |= 1 ; else state[i] &= 254 ;
  announce() ;
  if (verbosity >= 2)
    strerr_warni4x("service ", db->string + db->services[i].name, h ? "started" : "stopped", " successfully") ;
  broadcast_success(i, h) ;
}

static void on_failure (unsigned int i, int h, int crashed, unsigned int code)
{
  if (verbosity)
  {
    char fmt[UINT_FMT] ;
    fmt[uint_fmt(fmt, code)] = 0 ;
    strerr_warnw7x("service ", db->string + db->services[i].name, h ? "started" : "stopped", " unsuccessfully", ": ", crashed ? " crashed with signal " : " exited ", fmt) ;
  }
}

static int handle_signals (int h)
{
  int ok = 1 ;
  for (;;)
  {
    switch (selfpipe_read())
    {
      case -1 : strerr_diefu1sys(111, "selfpipe_read()") ;
      case 0 : return ok ;
      case SIGCHLD :
      {
        for (;;)
        {
          unsigned int j = 0 ;
          int wstat ;
          register pid_t r = wait_nohang(&wstat) ;
          if (r < 0)
            if (errno == ECHILD) break ;
            else strerr_diefu1sys(111, "wait for children") ;
          else if (!r) break ;
          for (; j < npids ; j++) if (pidindex[j].pid == r) break ;
          if (j < npids)
          {
            unsigned int i = pidindex[j].i ;
            pidindex[j] = pidindex[--npids] ;
            if (!WIFSIGNALED(wstat) && !WEXITSTATUS(wstat))
              on_success(i, h) ;
            else
            {
              ok = 0 ;
              on_failure(i, h, WIFSIGNALED(wstat), WIFSIGNALED(wstat) ? WTERMSIG(wstat) : WEXITSTATUS(wstat)) ;
            }
          }
        }
        break ;
      }
      default : strerr_dief1x(101, "inconsistent signal state") ;
    }
  }
}

static int doit (int spfd, int h)
{
  iopause_fd x = { .fd = spfd, .events = IOPAUSE_READ } ;
  int exitcode = 0 ;
  unsigned int i = n ;
  pidindex_t pidindexblob[n] ;
  unsigned int pendingdepsblob[n] ;
  pidindex = pidindexblob ;
  pendingdeps = pendingdepsblob ;

  if (verbosity >= 3)
    strerr_warni2x("bringing selected services ", h ? "up" : "down") ;
 
  while (i--)
  {
    state[i] &= 251 ;
    pendingdeps[i] = db->services[i].ndeps[h] ;
  }
  i = n ;
  while (i--) if (state[i] & 2) examine(i, h) ;

  for (;;)
  {
    register int r ;
    buffer_flush(buffer_1) ;
    if (!npids) break ;
    r = iopause_g(&x, 1, &deadline) ;
    if (r < 0) strerr_diefu1sys(111, "iopause") ;
    if (!r) strerr_dief1x(2, "timed out") ;
    if (!handle_signals(h)) exitcode = 1 ;
  }
  return exitcode ;
}

static void invert_selection (void)
{
  register unsigned int i = n ;

  if (verbosity >= 3)
    strerr_warni1x("inverting the selection") ;

  while (i--) state[i] ^= 2 ;
}

int main (int argc, char const *const *argv)
{
  what_t what = { .up = 1 } ;
  PROG = "s6-rc" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "v:n:t:l:udpaCLAS", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 'n' :
        {
          unsigned int d ;
          if (!uint0_scan(l.arg, &d)) dieusage() ;
          dryrun[uint_fmt(dryrun, d)] = 0 ;
          break ;
        }
        case 't' : if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        case 'l' : live = l.arg ; break ;
        case 'u' : what.up = 1 ; break ;
        case 'd' : what.up = 0 ; break ;
        case 'p' : what.prune = 1 ; break ;
        case 'a' : what.selectlive = 1 ; break ;
        case 'C' : what.check = 1 ; break ;
        case 'L' : what.list = 1 ; break ;
        case 'A' : what.listall = 1 ; break ;
        case 'S' : what.change = 1 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&deadline, t) ;
    else deadline = tain_infinite_relative ;
  }
  if (!what.list && !what.listall) what.change = 1 ;
  if (!tain_now_g())
    strerr_warnwu1x("get correct TAI time. (Do you have a valid leap seconds file?)") ;
  tain_add_g(&deadline, &deadline) ;
  livelen = str_len(live) ;

  {
    int livelock ;
    int fdcompiled ;
    s6rc_db_t dbblob ;
    char dbfn[livelen + 10] ;
    db = &dbblob ;
    if (verbosity >= 3)
      strerr_warni3x("reading services database in ", live, "/compiled") ;


   /* Take the live lock */

    byte_copy(dbfn, livelen, live) ;
    byte_copy(dbfn + livelen, 6, "/lock") ;
    livelock = open_write(dbfn) ;
    if (livelock < 0) strerr_diefu2sys(111, "open ", dbfn) ;
    if (coe(livelock) < 0) strerr_diefu2sys(111, "coe ", dbfn) ;
    if (lock_ex(livelock) < 0) strerr_diefu2sys(111, "lock ", dbfn) ;


   /* Read the sizes of the compiled db */

    byte_copy(dbfn + livelen + 1, 9, "compiled") ;
    fdcompiled = open_readb(dbfn) ;
    if (!s6rc_db_read_sizes(fdcompiled, &dbblob))
      strerr_diefu3sys(111, "read ", dbfn, "/n") ;


   /* Allocate enough stack for the db */

    {
      unsigned int n = dbblob.nshort + dbblob.nlong ;
      s6rc_service_t serviceblob[n] ;
      char *argvblob[dbblob.nargvs] ;
      uint32 depsblob[dbblob.ndeps << 1] ;
      char stringblob[dbblob.stringlen] ;
      unsigned char stateblob[n] ;
      register int r ;

      dbblob.services = serviceblob ;
      dbblob.argvs = argvblob ;
      dbblob.deps = depsblob ;
      dbblob.string = stringblob ;
      state = stateblob ;
      byte_zero(state, n) ;


     /* Read the db from the file */

      r = s6rc_db_read(fdcompiled, &dbblob) ;
      if (r < 0) strerr_diefu3sys(111, "read ", dbfn, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", dbfn, "/db") ;


     /* Check the db consistency */

      if (what.check)
      {
        unsigned int problem, w ;
        if (verbosity >= 3)
          strerr_warni1x("checking database consistency") ;

        if (!s6rc_db_check_revdeps(&dbblob))
          strerr_dief3x(4, "invalid service database in ", dbfn, ": direct and reverse dependencies are mismatched") ;
        w = s6rc_db_check_depcycles(&dbblob, 1, &problem) ;
        if (w < n)
          strerr_dief6x(4, "invalid service database in ", dbfn, ": service ", stringblob + serviceblob[w].name, " has a dependency cycle involving ", stringblob + serviceblob[problem].name) ;
      }


     /* Resolve the args and add them to the selection */

      if (verbosity >= 3)
        strerr_warni1x("resolving command line arguments") ;

      {
        cdb_t c = CDB_ZERO ;
        int fd = open_readatb(fdcompiled, "resolve.cdb") ;
        if (fd < 0) strerr_diefu3sys(111, "open ", dbfn, "/resolve.cdb") ;
        if (!cdb_init_map(&c, fd, 1))
          strerr_diefu3sys(111, "cdb_init ", dbfn, "/resolve.cdb") ;
        for (; *argv ; argv++)
        {
          unsigned int len ;
          register int r ;
          if (verbosity >= 4) strerr_warnt2x("looking up ", *argv) ;
          r = cdb_find(&c, *argv, str_len(*argv)) ;
          if (r < 0) strerr_diefu3sys(111, "read ", dbfn, "/resolve.cdb") ;
          if (!r)
            strerr_dief4x(3, *argv, " is not a recognized identifier in ", dbfn, "/resolve.cdb") ;
          if (cdb_datalen(&c) & 3)
            strerr_dief3x(4, "invalid resolve database in ", dbfn, "/resolve.cdb") ;
          len = cdb_datalen(&c) >> 2 ;
          if (len > n)
            strerr_dief3x(4, "invalid resolve database in ", dbfn, "/resolve.cdb") ;
          {
            char pack[cdb_datalen(&c) + 1] ;
            char const *p = pack ;
            if (cdb_read(&c, pack, cdb_datalen(&c), cdb_datapos(&c)) < 0)
            if (r < 0) strerr_diefu3sys(111, "read ", dbfn, "/resolve.cdb") ;
            while (len--)
            {
              uint32 x ;
              uint32_unpack_big(p, &x) ; p += 4 ;
              if (x >= n)
                strerr_dief3x(4, "invalid resolve database in ", dbfn, "/resolve.cdb") ;
              state[x] |= 2 ;
              if (verbosity >= 4)
              {
                char fmt[UINT32_FMT] ;
                fmt[uint32_fmt(fmt, x)] = 0 ;
                strerr_warnt2x("adding service ", fmt) ;
              }
            }
          }
        }
        cdb_free(&c) ;
        close(fd) ;
      }
      close(fdcompiled) ;


     /* Read live state in bit 0 of state */

      if (verbosity >= 3)
        strerr_warni2x("reading current state in ", live) ;

      byte_copy(dbfn + livelen + 1, 6, "state") ;
      {
        char tmpstate[n] ;
        r = openreadnclose(dbfn, tmpstate, n) ;
        if (r != n) strerr_diefu2sys(111, "read ", dbfn) ;
        {
          register unsigned int i = n ;
          while (i--) if (tmpstate[i]) state[i] |= 1 ;
        }
      }


     /* Add live state to selection */

      if (what.selectlive)
      {
        register unsigned int i = n ;
        while (i--) if (state[i] & 1) state[i] |= 2 ;
      }


     /* Print the selection before closure */

      if (what.list) print_services() ;


     /* Compute the closure */

      if (verbosity >= 3)
        strerr_warni1x("computing the final set of services") ;

      s6rc_graph_closure(db, state, 1, what.up) ;


     /* Print the selection after closure */

      if (what.listall) print_services() ;


     /* Perform a state change */

      if (what.change)
      {
        int spfd = selfpipe_init() ;
        if (spfd < 0) strerr_diefu1sys(111, "init selfpipe") ;
        if (selfpipe_trap(SIGCHLD) < 0)
            strerr_diefu1sys(111, "trap SIGCHLD") ;

        if (what.prune)
        {
          if (what.up) invert_selection() ;
          r = doit(spfd, 0) ;
          if (r) return r ;
          invert_selection() ;
          return doit(spfd, 1) ;
        }
        else return doit(spfd, what.up) ;
      }
    }
  }
  return 0 ;
}
