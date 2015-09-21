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
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc [ -v verbosity ] [ -n dryrunthrottle ] [ -t timeout ] [ -l live ] [ -u | -d ] [ -p ] [ -a ] help|list|listall|change [ servicenames... ]"
#define dieusage() strerr_dieusage(100, USAGE)

typedef struct pidindex_s pidindex_t ;
struct pidindex_s
{
  pid_t pid ;
  unsigned int i ;
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
  char tmpstate[n] ;
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
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static unsigned int compute_timeout (unsigned int i, int h)
{
  unsigned int t = db->services[i].timeout[h] ;
  int globalt ;
  tain_t globaltto ;
  tain_sub(&globaltto, &deadline, &STAMP) ;
  globalt = tain_to_millisecs(&globaltto) ;
  if (!globalt) globalt = 1 ;
  if (globalt > 0 && (!t || (unsigned int)globalt < t))
    t = (unsigned int)globalt ;
  return t ;
}

static pid_t start_oneshot (unsigned int i, int h)
{
  unsigned int m = 0 ;
  char const *newargv[11 + !!dryrun[0] * 6] ;
  char tfmt[UINT32_FMT] ;
  char vfmt[UINT_FMT] ;
  char ifmt[UINT_FMT] ;
  char socketfn[livelen + S6RC_ONESHOT_RUNNER_LEN + 12] ;
  byte_copy(socketfn, livelen, live) ;
  byte_copy(socketfn + livelen, 12 + S6RC_ONESHOT_RUNNER_LEN, "/scandir/" S6RC_ONESHOT_RUNNER "/s") ;
  tfmt[uint32_fmt(tfmt, compute_timeout(i, h))] = 0 ;
  vfmt[uint_fmt(vfmt, verbosity)] = 0 ;
  ifmt[uint_fmt(ifmt, i)] = 0 ;
  if (dryrun[0])
  {
    newargv[m++] = S6RC_BINPREFIX "s6-rc-dryrun" ;
    newargv[m++] = "-v" ;
    newargv[m++] = vfmt ;
    newargv[m++] = "-t" ;
    newargv[m++] = dryrun ;
    newargv[m++] = "--" ;
  }
  newargv[m++] = S6_EXTBINPREFIX "s6-sudo" ;
  newargv[m++] = verbosity >= 3 ? "-vel0" : "-el0" ;
  newargv[m++] = "-t" ;
  newargv[m++] = "2000" ;
  newargv[m++] = "-T" ;
  newargv[m++] = tfmt ;
  newargv[m++] = "--" ;
  newargv[m++] = socketfn ;
  newargv[m++] = h ? "up" : "down" ;
  newargv[m++] = ifmt ;
  newargv[m++] = 0 ;
  return child_spawn0(newargv[0], newargv, (char const *const *)environ) ;
}

static pid_t start_longrun (unsigned int i, int h)
{
  unsigned int svdlen = str_len(db->string + db->services[i].name) ;
  unsigned int m = 0 ;
  char fmt[UINT32_FMT] ;
  char vfmt[UINT_FMT] ;
  char servicefn[livelen + svdlen + 26] ;
  char const *newargv[7 + !!dryrun[0] * 6] ;
  byte_copy(servicefn, livelen, live) ;
  byte_copy(servicefn + livelen, 9, "/scandir/") ;
  byte_copy(servicefn + livelen + 9, svdlen, db->string + db->services[i].name) ;
  if (h)
  {
    byte_copy(servicefn + livelen + 9 + svdlen, 17, "/notification-fd") ;
    if (access(servicefn, F_OK) < 0)
    {
      h = 2 ;
      if (verbosity >= 2 && errno != ENOENT)
        strerr_warnwu2sys("access ", servicefn) ;
    }
  }
  servicefn[livelen + 9 + svdlen] = 0 ;
  fmt[uint32_fmt(fmt, compute_timeout(i, !!h))] = 0 ;  
  vfmt[uint_fmt(vfmt, verbosity)] = 0 ;
  if (dryrun[0])
  {
    newargv[m++] = S6RC_BINPREFIX "s6-rc-dryrun" ;
    newargv[m++] = "-v" ;
    newargv[m++] = vfmt ;
    newargv[m++] = "-t" ;
    newargv[m++] = dryrun ;
    newargv[m++] = "--" ;
  }
  newargv[m++] = S6_EXTBINPREFIX "s6-svc" ;
  newargv[m++] = h ? h == 2 ? "-uwu" : "-uwU" : "-dwD" ;
  newargv[m++] = "-T" ;
  newargv[m++] = fmt ;
  newargv[m++] = "--" ;
  newargv[m++] = servicefn ;
  newargv[m++] = 0 ;
  return child_spawn0(newargv[0], newargv, (char const *const *)environ) ;
}

static void success_longrun (unsigned int i, int h)
{
  if (!dryrun[0])
  {
    unsigned int svdlen = str_len(db->string + db->services[i].name) ;
    char fn[livelen + svdlen + 15] ;
    byte_copy(fn, livelen, live) ;
    byte_copy(fn + livelen, 9, "/scandir/") ;
    byte_copy(fn + livelen + 9, svdlen, db->string + db->services[i].name) ;
    byte_copy(fn + livelen + 9 + svdlen, 6, "/down") ;
    if (h)
    {
      if (unlink(fn) < 0 && verbosity)
        strerr_warnwu2sys("unlink ", fn) ;
    }
    else
    {
      int fd = open_trunc(fn) ;
      if (fd < 0)
      {
        if (verbosity)
          strerr_warnwu2sys("touch ", fn) ;
      }
      else fd_close(fd) ;
    }
  }
}

static void broadcast_success (unsigned int, int) ;

static void examine (unsigned int i, int h)
{
  if (state[i] & 2 && !pendingdeps[i] && !(state[i] & 4))
  {
    char const *name = db->string + db->services[i].name ;
    state[i] |= 4 ;
    if ((state[i] & 1) == h)
    {
      if (verbosity >= 2)
        strerr_warni4x("processing service ", name, ": already ", h ? "up" : "down") ;
      broadcast_success(i, h) ;
    }
    else
    {
      pidindex[npids].pid = i < db->nlong ? start_longrun(i, h) : start_oneshot(i, h) ;
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
  if (i < db->nlong) success_longrun(i, h) ;
  if (h) state[i] |= 1 ; else state[i] &= 254 ;
  announce() ;
  if (verbosity >= 2)
    strerr_warni5x(dryrun[0] ? "simulation: " : "", "service ", db->string + db->services[i].name, h ? " started" : " stopped", " successfully") ;
  broadcast_success(i, h) ;
}

static void on_failure (unsigned int i, int h, int crashed, unsigned int code)
{
  if (verbosity)
  {
    char fmt[UINT_FMT] ;
    fmt[uint_fmt(fmt, code)] = 0 ;
    strerr_warnwu7x(dryrun[0] ? "pretend to " : "", h ? "start" : "stop", " service ", db->string + db->services[i].name, ": command ", crashed ? "crashed with signal " : "exited ", fmt) ;
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
  while (i--) examine(i, h) ;

  for (;;)
  {
    register int r ;
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
  while (i--) state[i] ^= 2 ;
}

static inline unsigned int lookup (char const *const *table, char const *command)
{
  register unsigned int i = 0 ;
  for (; table[i] ; i++) if (!str_diff(command, table[i])) break ;
  return i ;
}

static inline unsigned int parse_command (char const *command)
{
  static char const *const command_table[5] =
  {
    "help",
    "list",
    "listall",
    "change",
    0
  } ;
  register unsigned int i = lookup(command_table, command) ;
  if (!command_table[i]) dieusage() ;
  return i ;
}

static inline void print_help (void)
{
  static char const *help =
"s6-rc help\n"
"s6-rc [ -l live ] [ -a ] list [ servicenames... ]\n"
"s6-rc [ -l live ] [ -a ] [ -u | -d ] listall [ servicenames... ]\n"
"s6-rc [ -l live ] [ -a ] [ -u | -d ] [ -p ] [ -v verbosity ] [ -t timeout ] [ -n dryrunthrottle ] change [ servicenames... ]\n" ;
  if (buffer_putsflush(buffer_1, help) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

int main (int argc, char const *const *argv)
{
  int up = 1, prune = 0, selectlive = 0, takelocks = 1 ;
  unsigned int what ;
  PROG = "s6-rc" ;
  {
    unsigned int t = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "v:n:t:l:udpaX", &l) ;
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
        case 'u' : up = 1 ; break ;
        case 'd' : up = 0 ; break ;
        case 'p' : prune = 1 ; break ;
        case 'a' : selectlive = 1 ; break ;
        case 'X' : takelocks = 0 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&deadline, t) ;
    else deadline = tain_infinite_relative ;
  }
  if (!argc) dieusage() ;
  what = parse_command(*argv++) ;
  if (!what)
  {
    print_help() ;
    return 0 ;
  }

  livelen = str_len(live) ;

  {
    int fdcompiled ;
    s6rc_db_t dbblob ;
    char dbfn[livelen + 10] ;
    db = &dbblob ;
    byte_copy(dbfn, livelen, live) ;
    byte_copy(dbfn + livelen, 10, "/compiled") ;


   /* Take the locks on live and compiled */

    if (takelocks)
    {
      int livelock, compiledlock ;
      if (!s6rc_lock(live, 1 + (what >= 3), &livelock, dbfn, 1, &compiledlock))
        strerr_diefu1sys(111, "take locks") ;
      if (coe(livelock) < 0)
        strerr_diefu3sys(111, "coe ", live, "/lock") ;
      if (compiledlock >= 0 && coe(compiledlock) < 0)
        strerr_diefu4sys(111, "coe ", live, "/compiled", "/lock") ;
     /* locks leak, but we don't care */
    }


   /* Read the sizes of the compiled db */

    fdcompiled = open_readb(dbfn) ;
    if (!s6rc_db_read_sizes(fdcompiled, &dbblob))
      strerr_diefu3sys(111, "read ", dbfn, "/n") ;
    n = dbblob.nshort + dbblob.nlong ;


   /* Allocate enough stack for the db */

    {
      int spfd ;
      s6rc_service_t serviceblob[n] ;
      char const *argvblob[dbblob.nargvs] ;
      uint32 depsblob[dbblob.ndeps << 1] ;
      char stringblob[dbblob.stringlen] ;
      unsigned char stateblob[n] ;
      register int r ;

      dbblob.services = serviceblob ;
      dbblob.argvs = argvblob ;
      dbblob.deps = depsblob ;
      dbblob.string = stringblob ;
      state = stateblob ;


     /* Read live state in bit 0 of state */

      byte_copy(dbfn + livelen + 1, 6, "state") ;
      {
        r = openreadnclose(dbfn, (char *)state, n) ;
        if (r != n) strerr_diefu2sys(111, "read ", dbfn) ;
        {
          register unsigned int i = n ;
          while (i--) state[i] &= 1 ;
        }
      }


     /* Read the db from the file */

      r = s6rc_db_read(fdcompiled, &dbblob) ;
      if (r < 0) strerr_diefu3sys(111, "read ", dbfn, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", dbfn, "/db") ;


     /* Resolve the args and add them to the selection */

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
            }
          }
        }
        cdb_free(&c) ;
        close(fd) ;
      }
      close(fdcompiled) ;


     /* Add live state to selection */

      if (selectlive)
      {
        register unsigned int i = n ;
        while (i--) if (state[i] & 1) state[i] |= 2 ;
      }


     /* Print the selection before closure */

      if (what == 1)
      {
        if (!up) invert_selection() ;
        print_services() ;
        return 0 ;
      }

      s6rc_graph_closure(db, state, 1, up) ;


     /* Print the selection after closure */

      if (what == 2)
      {
        print_services() ;
        return 0 ;
      }

      if (!tain_now_g())
        strerr_warnwu1x("get correct TAI time. (Do you have a valid leap seconds file?)") ;
      tain_add_g(&deadline, &deadline) ;


     /* Perform a state change */

      spfd = selfpipe_init() ;
      if (spfd < 0) strerr_diefu1sys(111, "init selfpipe") ;
      if (selfpipe_trap(SIGCHLD) < 0)
          strerr_diefu1sys(111, "trap SIGCHLD") ;

      if (prune)
      {
        if (up) invert_selection() ;
        r = doit(spfd, 0) ;
        if (r) return r ;
        invert_selection() ;
        return doit(spfd, 1) ;
      }
      else return doit(spfd, up) ;
    }
  }
}
