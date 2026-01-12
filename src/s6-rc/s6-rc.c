/* ISC license. */

#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include <skalibs/posixplz.h>
#include <skalibs/envexec.h>
#include <skalibs/stddjb.h>
#include <skalibs/unix-transactional.h>

#include <s6/config.h>
#include <s6/supervise.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc [ -v verbosity ] [ -n dryrunthrottle ] [ -t timeout ] [ -l live ] [ -b ] [ -E | -e ] [ -u | -d | -D ] [ -p ] [ -a ] help|list|listall|diff|start|stop|change [ servicenames... ]"
#define dieusage() strerr_dieusage(100, USAGE)

typedef struct pidindex_s pidindex_t ;
struct pidindex_s
{
  pid_t pid ;
  unsigned int i ;
} ;

enum what_e
{
  WHAT_HELP,
  WHAT_LIST,
  WHAT_LISTALL,
  WHAT_DIFF,
  WHAT_CHANGE,
  WHAT_START,
  WHAT_STOP,
} ;

struct what_s
{
  char const *s ;
  enum what_e e ;
} ;

enum golb_e
{
  GOLB_DOWN = 0x01,
  GOLB_HIDEESSENTIALS = 0x02,
  GOLB_PRUNE = 0x04,
  GOLB_SELECTLIVE = 0x08,
  GOLB_BLOCK = 0x10,
  GOLB_NOLOCK = 0x80
} ;

enum gola_e
{
  GOLA_VERBOSITY,
  GOLA_DRYRUN,
  GOLA_TIMEOUT,
  GOLA_LIVEDIR,
  GOLA_N
} ;

static uint64_t wgolb = 0 ;
static unsigned int verbosity = 1 ;
static char const *live = S6RC_LIVEDIR ;
static size_t livelen ;
static pidindex_t *pidindex ;
static unsigned int npids = 0 ;
static s6rc_db_t *db ;
static unsigned int n ;
static unsigned char *state ;
static unsigned int *pendingdeps ;
static tain deadline = TAIN_INFINITE_RELATIVE ;
static int lameduck = 0 ;
static char dryrun[UINT_FMT] = "" ;

static inline void announce (void)
{
  unsigned int i = n ;
  char fn[livelen + 7] ;
  char tmpstate[n] ;
  if (dryrun[0]) return ;
  memcpy(fn, live, livelen) ;
  memcpy(fn + livelen, "/state", 7) ;
  while (i--) tmpstate[i] = !!(state[i] & 1) ;
  if (!openwritenclose_suffix(fn, tmpstate, n, ".new"))
    strerr_diefu2sys(111, "write ", fn) ;
}

static inline int print_services (void)
{
  for (unsigned int i = 0 ; i < n ; i++)
  {
    if (wgolb & GOLB_HIDEESSENTIALS && db->services[i].flags & S6RC_DB_FLAG_ESSENTIAL) continue ;
    if (state[i] & 2)
    {
      if (buffer_puts(buffer_1, db->string + db->services[i].name) < 0
       || buffer_put(buffer_1, "\n", 1) < 0) goto err ;
    }
  }
  if (!buffer_flush(buffer_1)) goto err ;
  return 0 ;

 err:
  strerr_diefu1sys(111, "write to stdout") ;
}

static inline int print_diff (void)
{
  s6_svstatus_t status ;
  int e = 0 ;
  unsigned int i = 0 ;
  for (; i < db->nlong ; i++)
  {
    size_t namelen = strlen(db->string + db->services[i].name) ;
    char fn[livelen + namelen + 14] ;
    memcpy(fn, live, livelen) ;
    memcpy(fn + livelen, "/servicedirs/", 13) ;
    memcpy(fn + livelen + 13, db->string + db->services[i].name, namelen + 1) ;
    if (!s6_svstatus_read(fn, &status))
      strerr_diefu2sys(111, "read longrun status for ", fn) ;
    if ((state[i] & 1) != status.flagwantup)
    {
      e = 1 ;
      if (buffer_put(buffer_1, status.flagwantup ? "+" : "-", 1) < 1
       || buffer_put(buffer_1, db->string + db->services[i].name, namelen) < 0
       || buffer_put(buffer_1, "\n", 1) < 1) goto err ;
    }
  }
  if (!buffer_flush(buffer_1)) goto err ;
  return e ;

 err:
  strerr_diefu1sys(111, "write to stdout") ;
}

static uint32_t compute_timeout (unsigned int i, int h)
{
  uint32_t t = db->services[i].timeout[h] ;
  int globalt ;
  tain globaltto ;
  tain_sub(&globaltto, &deadline, &STAMP) ;
  globalt = tain_to_millisecs(&globaltto) ;
  if (!globalt) globalt = 1 ;
  if (globalt > 0 && (!t || (unsigned int)globalt < t))
    t = (uint32_t)globalt ;
  return t ;
}

static inline pid_t start_oneshot (unsigned int i, int h)
{
  unsigned int m = 0 ;
  char const *newargv[11 + !!dryrun[0] * 8] ;
  char tfmt[UINT32_FMT] ;
  char vfmt[UINT_FMT] ;
  char ifmt[UINT_FMT] ;
  char socketfn[livelen + S6RC_ONESHOT_RUNNER_LEN + 16] ;
  memcpy(socketfn, live, livelen) ;
  memcpy(socketfn + livelen, "/servicedirs/" S6RC_ONESHOT_RUNNER "/s", 16 + S6RC_ONESHOT_RUNNER_LEN) ;
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
  newargv[m++] = verbosity >= 4 ? "-vel0" : "-el0" ;
  newargv[m++] = "-t" ;
  newargv[m++] = "30000" ;
  newargv[m++] = "-T" ;
  newargv[m++] = tfmt ;
  newargv[m++] = "--" ;
  newargv[m++] = socketfn ;
  newargv[m++] = h ? "up" : "down" ;
  newargv[m++] = ifmt ;
  if (dryrun[0])
  {
    newargv[m++] = " #" ;
    newargv[m++] = db->string + db->services[i].name ;
  }
  newargv[m++] = 0 ;
  return cspawn(newargv[0], newargv, (char const *const *)environ, CSPAWN_FLAGS_SELFPIPE_FINISH, 0, 0) ;
}

static inline pid_t start_longrun (unsigned int i, int h)
{
  size_t svdlen = strlen(db->string + db->services[i].name) ;
  unsigned int m = 0 ;
  char fmt[UINT32_FMT] ;
  char vfmt[UINT_FMT] ;
  char servicefn[livelen + svdlen + 30] ;
  char const *newargv[7 + !!dryrun[0] * 6] ;
  memcpy(servicefn, live, livelen) ;
  memcpy(servicefn + livelen, "/servicedirs/", 13) ;
  memcpy(servicefn + livelen + 13, db->string + db->services[i].name, svdlen) ;
  if (h)
  {
    memcpy(servicefn + livelen + 13 + svdlen, "/notification-fd", 17) ;
    if (access(servicefn, F_OK) < 0)
    {
      h = 2 ;
      if (verbosity >= 2 && errno != ENOENT)
        strerr_warnwu2sys("access ", servicefn) ;
    }
  }
  servicefn[livelen + 13 + svdlen] = 0 ;
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
  return cspawn(newargv[0], newargv, (char const *const *)environ, CSPAWN_FLAGS_SELFPIPE_FINISH, 0, 0) ;
}

static inline void success_longrun (unsigned int i, int h)
{
  if (!dryrun[0])
  {
    size_t svdlen = strlen(db->string + db->services[i].name) ;
    char fn[livelen + svdlen + 19] ;
    memcpy(fn, live, livelen) ;
    memcpy(fn + livelen, "/servicedirs/", 13) ;
    memcpy(fn + livelen + 13, db->string + db->services[i].name, svdlen) ;
    memcpy(fn + livelen + 13 + svdlen, "/down", 6) ;
    if (h)
    {
      if (unlink(fn) == -1 && verbosity)
        strerr_warnwu2sys("unlink ", fn) ;
    }
    else
    {
      int fd = open_trunc(fn) ;
      if (fd == -1)
      {
        if (verbosity)
          strerr_warnwu2sys("touch ", fn) ;
      }
      else fd_close(fd) ;
    }
  }
}

static inline void failure_longrun (unsigned int i, int h)
{
  if (h && !dryrun[0])
  {
    size_t svdlen = strlen(db->string + db->services[i].name) ;
    char fn[livelen + svdlen + 14] ;
    char const *newargv[5] = { S6_EXTBINPREFIX "s6-svc", "-d", "--", fn, 0 } ;
    memcpy(fn, live, livelen) ;
    memcpy(fn + livelen, "/servicedirs/", 13) ;
    memcpy(fn + livelen + 13, db->string + db->services[i].name, svdlen + 1) ;
    if (!cspawn(newargv[0], newargv, (char const *const *)environ, CSPAWN_FLAGS_SELFPIPE_FINISH, 0, 0))
      strerr_warnwu2sys("spawn ", newargv[0]) ;
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
      if (verbosity >= 3)
        strerr_warni4x("service ", name, ": already ", h ? "up" : "down") ;
      broadcast_success(i, h) ;
    }
    else if (!h && !(wgolb & GOLB_HIDEESSENTIALS) && db->services[i].flags & S6RC_DB_FLAG_ESSENTIAL)
    {
      if (verbosity)
        strerr_warnw3x("service ", name, " is marked as essential, not stopping it") ;
    }
    else
    {
      pidindex[npids].pid = i < db->nlong ? start_longrun(i, h) : start_oneshot(i, h) ;
      if (pidindex[npids].pid)
      {
        pidindex[npids++].i = i ;
        if (verbosity >= 2)
        {
          strerr_warni4x("service ", name, ": ", h ? "starting" : "stopping") ;
        }
      }
      else
      {
        if (verbosity)
          strerr_warnwu2sys("spawn subprocess for ", name) ;
        if (verbosity >= 2)
          strerr_warni4x("service ", name, ": failed to ", h ? "start" : "stop") ;
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

static inline void on_success (unsigned int i, int h)
{
  if (i < db->nlong) success_longrun(i, h) ;
  if (h) state[i] |= 1 ; else state[i] &= 254 ;
  announce() ;
  if (verbosity >= 2)
    strerr_warni5x(dryrun[0] ? "simulation: " : "", "service ", db->string + db->services[i].name, " successfully st", h ? "arted" : "opped") ;
  if (!lameduck) broadcast_success(i, h) ;
}

static inline void on_failure (unsigned int i, int h, int crashed, unsigned int code)
{
  if (i < db->nlong) failure_longrun(i, h) ;
  if (verbosity)
  {
    char fmt[UINT_FMT] ;
    fmt[uint_fmt(fmt, code)] = 0 ;
    strerr_warnwu7x(dryrun[0] ? "pretend to " : "", h ? "start" : "stop", " service ", db->string + db->services[i].name, ": command ", crashed ? "crashed with signal " : "exited ", fmt) ;
  }
}

/*
static inline void kill_oneshots (void)
{
  char fn[livelen + S6RC_ONESHOT_RUNNER_LEN + 14] ;
  char const *newargv[5] = { S6_EXTBINPREFIX "s6-svc", "-h", "--", fn, 0 } ;
  memcpy(fn, live, livelen) ;
  memcpy(fn + livelen, "/servicedirs/", 13) ;
  memcpy(fn + livelen + 13, S6RC_ONESHOT_RUNNER, S6RC_ONESHOT_RUNNER_LEN + 1) ;
  if (!cspawn(newargv[0], newargv, (char const *const *)environ, CSPAWN_FLAGS_SELFPIPE_FINISH, 0, 0))
    strerr_warnwu2sys("spawn ", newargv[0]) ;
}
*/

static inline void kill_longruns (void)
{
  unsigned int j = npids ;
  while (j--) if (pidindex[j].i < db->nlong)
    kill(pidindex[j].pid, SIGTERM) ;
}

static inline int handle_signals (int h)
{
  int ok = 1 ;
  for (;;)
  {
    int sig = selfpipe_read() ;
    switch (sig)
    {
      case -1 : strerr_diefu1sys(111, "selfpipe_read()") ;
      case 0 : return ok ;
      case SIGCHLD :
        for (;;)
        {
          unsigned int j = 0 ;
          int wstat ;
          pid_t r = wait_nohang(&wstat) ;
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
      case SIGTERM :
      case SIGINT :
        if (verbosity >= 2)
          strerr_warnw3x("received ", sig_name(sig), ", aborting longrun transitions and exiting asap") ;
        /* kill_oneshots() ; */
        kill_longruns() ;
        lameduck = 1 ;
        break ;
      default : strerr_dief1x(101, "inconsistent signal state") ;
    }
  }
}

static int doit (int h)
{
  iopause_fd x = { .fd = selfpipe_fd(), .events = IOPAUSE_READ } ;
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

  while (npids)
  {
    int r = iopause_g(&x, 1, &deadline) ;
    if (r < 0) strerr_diefu1sys(111, "iopause") ;
    if (!r) strerr_dief1x(2, "timed out") ;
    if (!handle_signals(h)) exitcode = 1 ;
  }
  return exitcode ;
}

static void invert_selection (void)
{
  unsigned int i = n ;
  while (i--) state[i] ^= 2 ;
}

static inline enum what_e parse_command (char const *command)
{
  static struct what_s const command_table[] =
  {
    { .s = "change", .e = WHAT_CHANGE },
    { .s = "diff", .e = WHAT_DIFF },
    { .s = "help", .e = WHAT_HELP },
    { .s = "list", .e = WHAT_LIST },
    { .s = "listall", .e = WHAT_LISTALL },
    { .s = "start", .e = WHAT_START },
    { .s = "stop", .e = WHAT_STOP },
  } ;
  struct what_s const *p = bsearch(command, command_table, sizeof(command_table)/sizeof(struct what_s), sizeof(struct what_s), &stringkey_bcmp) ;
  if (!p) strerr_dief2x(100, "unknown command: ", command) ;
  return p->e ;
}

static inline void print_help (void)
{
  static char const *help =
"s6-rc help\n"
"s6-rc [ -l live ] [ -a ] list [ servicenames... ]\n"
"s6-rc [ -l live ] [ -a ] [ -u | -d ] listall [ servicenames... ]\n"
"s6-rc [ -l live ] diff\n"
"s6-rc [ -l live ] [ -a ] [ -u | -d | -D ] [ -p ] [ -v verbosity ] [ -t timeout ] [ -n dryrunthrottle ] change [ servicenames... ]\n" ;
  if (buffer_putsflush(buffer_1, help) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

int main (int argc, char const *const *argv)
{
  static gol_bool const rgolb[] =
  {
    { .so = 'u', .lo = "up", .clear = GOLB_DOWN, .set = 0 },
    { .so = 'd', .lo = "down", .clear = 0, .set = GOLB_DOWN },
    { .so = 'D', .lo = "all-down", .clear = 0, .set = GOLB_DOWN | GOLB_HIDEESSENTIALS },
    { .so = 'p', .lo = "prune", .clear = 0, .set = GOLB_PRUNE },
    { .so = 'a', .lo = "add", .clear = 0, .set = GOLB_SELECTLIVE },
    { .so = 'b', .lo = "block", .clear = 0, .set = GOLB_BLOCK },
    { .so = 'E', .lo = "with-essentials", .clear = GOLB_HIDEESSENTIALS, .set = 0 },
    { .so = 'e', .lo = "without-essentials", .clear = 0, .set = GOLB_HIDEESSENTIALS },
    { .so = 'X', .lo = "no-lock", .clear = 0, .set = GOLB_NOLOCK },
  } ;
  static gol_arg const rgola[] =
  {
    { .so = 'v', .lo = "verbosity", .i = GOLA_VERBOSITY },
    { .so = 'n', .lo = "dry-run", .i = GOLA_DRYRUN },
    { .so = 't', .lo = "timeout", .i = GOLA_TIMEOUT },
    { .so = 'l', .lo = "livedir", .i = GOLA_LIVEDIR },
  } ;
  char const *wgola[GOLA_N] = { 0 } ;
  enum what_e what ;
  PROG = "s6-rc" ;

  {
    unsigned int golc = GOL_main(argc, argv, rgolb, rgola, &wgolb, wgola) ;
    argc -= golc ; argv += golc ;
  }

  if (wgola[GOLA_VERBOSITY] && !uint0_scan(wgola[GOLA_VERBOSITY], &verbosity))
    strerr_dief1x(100, "verbosity must be an unsigned integer") ;
  if (wgola[GOLA_DRYRUN])
  {
    unsigned int d ;
    if (!uint0_scan(wgola[GOLA_DRYRUN], &d))
      strerr_dief1x(100, "dry-run must be an unsigned integer") ;
    dryrun[uint_fmt(dryrun, d)] = 0 ;
  }
  if (wgola[GOLA_TIMEOUT])
  {
    unsigned int t ;
    if (!uint0_scan(wgola[GOLA_TIMEOUT], &t))
      strerr_dief1x(100, "verbosity must be an unsigned integer") ;
    if (t) tain_from_millisecs(&deadline, t) ;
  }
  if (wgola[GOLA_LIVEDIR]) live = wgola[GOLA_LIVEDIR] ;

  if (!argc) dieusage() ;
  what = parse_command(*argv++) ;
  if (what == WHAT_HELP)
  {
    print_help() ;
    _exit(0) ;
  }
  if (what == WHAT_START)
  {
    what = WHAT_CHANGE ; wgolb &= ~(GOLB_DOWN | GOLB_PRUNE) ;
  }
  else if (what == WHAT_STOP)
  {
    what = WHAT_CHANGE ; wgolb &= ~GOLB_PRUNE ; wgolb |= GOLB_DOWN ;
  }

  livelen = strlen(live) ;

  {
    int fdcompiled ;
    s6rc_db_t dbblob ;
    char dbfn[livelen + 10] ;
    db = &dbblob ;
    memcpy(dbfn, live, livelen) ;
    memcpy(dbfn + livelen, "/compiled", 10) ;


   /* Take the locks on live and compiled */

    if (!(wgolb & GOLB_NOLOCK))
    {
      int livelock, compiledlock ;
      if (!s6rc_lock(live, 1 + (what >= 4), &livelock, dbfn, 1, &compiledlock, !!(wgolb & GOLB_BLOCK)))
        strerr_diefu1sys(111, "take locks") ;
      if (coe(livelock) < 0)
        strerr_diefu3sys(111, "coe ", live, "/lock") ;
      if (compiledlock >= 0 && coe(compiledlock) < 0)
        strerr_diefu4sys(111, "coe ", live, "/compiled", "/lock") ;
     /* locks leak, but we don't care */
    }


   /* Read the size of the compiled db */

    fdcompiled = open_readb(dbfn) ;
    if (!s6rc_db_read_sizes(fdcompiled, &dbblob))
      strerr_diefu3sys(111, "read ", dbfn, "/n") ;
    n = dbblob.nshort + dbblob.nlong ;


   /* Allocate enough stack for the db */

    {
      s6rc_service_t serviceblob[n] ;
      char const *argvblob[dbblob.nargvs] ;
      uint32_t depsblob[dbblob.ndeps << 1] ;
      uint32_t producersblob[dbblob.nproducers] ;
      char stringblob[dbblob.stringlen] ;
      unsigned char stateblob[n] ;

      dbblob.services = serviceblob ;
      dbblob.argvs = argvblob ;
      dbblob.deps = depsblob ;
      dbblob.producers = producersblob ;
      dbblob.string = stringblob ;
      state = stateblob ;


     /* Read live state in bit 0 of state */

      memcpy(dbfn + livelen + 1, "state", 6) ;
      {
        ssize_t r = openreadnclose(dbfn, (char *)state, n) ;
        if (r == -1) strerr_diefu2sys(111, "read ", dbfn) ;
        if (r < n) strerr_diefu2x(4, "read valid state in ", dbfn) ;
        {
          unsigned int i = n ;
          while (i--) state[i] &= 1 ;
        }
      }
      dbfn[livelen] = 0 ;


     /* Read the db from the file */
      {
        int r = s6rc_db_read(fdcompiled, &dbblob) ;
        if (r < 0) strerr_diefu3sys(111, "read ", dbfn, "/db") ;
        if (!r) strerr_dief3x(4, "invalid service database in ", dbfn, "/db") ;
      }


     /* s6-rc diff */

      if (what == WHAT_DIFF) _exit(print_diff()) ;


     /* Resolve the args and add them to the selection */

      {
        cdb c = CDB_ZERO ;
        if (!cdb_init_at(&c, fdcompiled, "resolve.cdb"))
          strerr_diefu3sys(111, "cdb_init ", dbfn, "/resolve.cdb") ;
        for (; *argv ; argv++)
        {
          cdb_data data ;
          int r = cdb_find(&c, &data, *argv, strlen(*argv)) ;
          if (r < 0) strerr_dief3x(4, "invalid cdb in ", dbfn, "/resolve.cdb") ;
          if (!r) strerr_dief4x(3, *argv, " is not a recognized identifier in ", dbfn, "/resolve.cdb") ;
          if (data.len & 3)
            strerr_dief3x(4, "invalid resolve database in ", dbfn, "/resolve.cdb") ;
          if (data.len >> 2 > n)
            strerr_dief3x(4, "invalid resolve database in ", dbfn, "/resolve.cdb") ;
          while (data.len)
          {
            uint32_t x ;
            uint32_unpack_big(data.s, &x) ; data.s += 4 ; data.len -= 4 ;
            if (x >= n)
              strerr_dief3x(4, "invalid resolve database in ", dbfn, "/resolve.cdb") ;
            state[x] |= 2 ;
          }
        }
        cdb_free(&c) ;
      }
      close(fdcompiled) ;


     /* Add live state to selection */

      if (wgolb & GOLB_SELECTLIVE)
      {
        unsigned int i = n ;
        while (i--) if (state[i] & 1) state[i] |= 2 ;
      }


     /* Print the selection before closure */

      if (what == WHAT_LIST)
      {
        if (wgolb & GOLB_DOWN) invert_selection() ;
        _exit(print_services()) ;
      }

      s6rc_graph_closure(db, state, 1, !(wgolb & GOLB_DOWN)) ;


     /* Print the selection after closure */

      if (what == WHAT_LISTALL) _exit(print_services()) ;

      tain_now_set_stopwatch_g() ;
      tain_add_g(&deadline, &deadline) ;


     /* Perform a state change */

      if (selfpipe_init() == -1)
        strerr_diefu1sys(111, "init selfpipe") ;

      {
        sigset_t set ;
        sigemptyset(&set) ;
        sigaddset(&set, SIGCHLD) ;
        sigaddset(&set, SIGTERM) ;
        sigaddset(&set, SIGINT) ;
        if (!selfpipe_trapset(&set))
          strerr_diefu1sys(111, "trap signals") ;
      }

      if (wgolb & GOLB_PRUNE)
      {
        int r ;
        if (!(wgolb & GOLB_DOWN)) invert_selection() ;
        r = doit(0) ;
        if (r) return r ;
        invert_selection() ;
        _exit(doit(1)) ;
      }
      else _exit(doit(!(wgolb & GOLB_DOWN))) ;
    }
  }
}
