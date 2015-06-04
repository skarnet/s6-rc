/* ISC license. */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <skalibs/uint32.h>
#include <skalibs/uint.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/bytestr.h>
#include <skalibs/cdb.h>
#include <skalibs/sgetopt.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/environ.h>
#include <skalibs/direntry.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>
#include <s6/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-db [ -u | -d ] [ -l live ] [ -c compiled ] command... (use s6-rc-db help for more information)"
#define dieusage() strerr_dieusage(100, USAGE)

static char const *compiled = 0 ;
static int fdcompiled = -1 ;
static s6rc_db_t *db ;

static void print_bundle_contents (char const *name)
{
  cdb_t c = CDB_ZERO ;
  int fd = open_readatb(fdcompiled, "resolve.cdb") ;
  register int r ;
  if (fd < 0) strerr_diefu3sys(111, "open ", compiled, "/resolve.cdb") ;
  if (!cdb_init_map(&c, fd, 1))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  r = cdb_find(&c, name, str_len(name)) ;
  if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/resolve.cdb") ;
  if (!r) strerr_dief3x(1, name, " is not a valid identifier in ", compiled) ;
  if (cdb_datalen(&c) == 4)
  {
    uint32 x ;
    char pack[4] ;
    if (cdb_read(&c, pack, 4, cdb_datapos(&c)) < 0)
      strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ; 
    uint32_unpack_big(pack, &x) ;
    if (x >= db->nshort + db->nlong)
      strerr_dief2x(4, "invalid database in ", compiled) ;
    if (!str_diff(name, db->string + db->services[x].name))
      strerr_dief5x(1, "in database ", compiled, ": identifier ", name, " represents an atomic service") ;
    if (buffer_puts(buffer_1, db->string + db->services[x].name) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  }
  else
  {
    unsigned int len = cdb_datalen(&c) >> 2 ;
    char pack[cdb_datalen(&c) + 1] ;
    register char const *p = pack ;
    if (cdb_datalen(&c) & 3)
      strerr_dief2x(4, "invalid database in ", compiled) ;
    if (cdb_read(&c, pack, cdb_datalen(&c), cdb_datapos(&c)) < 0)
      strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ; 
    while (len--)
    {
      uint32 x ;
      uint32_unpack_big(p, &x) ; p += 4 ;
      if (x >= db->nshort + db->nlong)
        strerr_dief2x(4, "invalid database in ", compiled) ;
      if (buffer_puts(buffer_1, db->string + db->services[x].name) < 0
       || buffer_put(buffer_1, "\n", 1) < 0)
        strerr_diefu1sys(111, "write to stdout") ;
    }
  }
  cdb_free(&c) ;
  close(fd) ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_services (unsigned int from, unsigned int to)
{
  for (; from < to ; from++)
  {
    if (buffer_puts(buffer_1, db->string + db->services[from].name) < 0
     || buffer_put(buffer_1, "\n", 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  }
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_all (int bundlesonly)
{
  cdb_t c = CDB_ZERO ;
  uint32 kpos ;
  int fd = open_readatb(fdcompiled, "resolve.cdb") ;
  if (fd < 0) strerr_diefu3sys(111, "open ", compiled, "/resolve.cdb") ;
  if (!cdb_init_map(&c, fd, 1))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  cdb_traverse_init(&c, &kpos) ;
  for (;;)
  {
    register int r = cdb_nextkey(&c, &kpos) ;
    char name[cdb_keylen(&c) + 1] ;
    if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/resolve.cdb") ;
    if (!r) break ;
    if (cdb_read(&c, name, cdb_keylen(&c), cdb_keypos(&c)) < 0)
      strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
    if (bundlesonly && cdb_datalen(&c) == 4)
    {
      uint32 x ;
      char pack[4] ;
      if (cdb_read(&c, pack, 4, cdb_datapos(&c)) < 0)
        strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
      uint32_unpack_big(pack, &x) ;
      if (x >= db->nshort + db->nlong)
        strerr_dief2x(4, "invalid database in ", compiled) ;
      if (!byte_diff(name, cdb_keylen(&c), db->string + db->services[x].name)
       && !db->string[db->services[x].name + cdb_keylen(&c)])
        continue ;
    }
    name[cdb_keylen(&c)] = '\n' ;
    if (buffer_put(buffer_1, name, cdb_keylen(&c) + 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  }
  cdb_free(&c) ;
  close(fd) ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static unsigned int resolve_service (char const *name)
{
  cdb_t c = CDB_ZERO ;
  int fd = open_readatb(fdcompiled, "resolve.cdb") ;
  uint32 x ;
  char pack[4] ;
  unsigned int len ;
  register int r ;
  if (fd < 0) strerr_diefu3sys(111, "open ", compiled, "/resolve.cdb") ;
  if (!cdb_init_map(&c, fd, 1))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  r = cdb_find(&c, name, str_len(name)) ;
  if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/resolve.cdb") ;
  if (!r) strerr_dief3x(1, name, " is not a valid identifier in ", compiled) ;
  len = cdb_datalen(&c) >> 2 ;
  if (cdb_datalen(&c) != 4) return db->nshort + db->nlong ;
  if (cdb_read(&c, pack, 4, cdb_datapos(&c)) < 0)
    strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
  uint32_unpack_big(pack, &x) ;
  cdb_free(&c) ;
  close(fd) ;
  if (x >= db->nshort + db->nlong)
    strerr_dief2x(4, "invalid database in ", compiled) ;
  if (str_diff(name, db->string + db->services[x].name))
    x = db->nshort + db->nlong ;
  return x ;
}

static void print_type (char const *name)
{
  unsigned int n = resolve_service(name) ;
  char const *s = n >= db->nshort + db->nlong ? "bundle" : db->services[n].type ? "longrun" : "oneshot" ;
  if (buffer_puts(buffer_1, s) < 0
   || buffer_putflush(buffer_1, "\n", 1) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_timeout (char const *name, int h)
{
  unsigned int n = resolve_service(name) ;
  unsigned int len ;
  char fmt[UINT32_FMT] ;
  if (n >= db->nshort + db->nlong)
    strerr_dief5x(1, "in database ", compiled, ": identifier ", name, " represents a bundle") ;
  len = uint32_fmt(fmt, db->services[n].timeout[h]) ;
  fmt[len++] = '\n' ;
  if (buffer_putflush(buffer_1, fmt, len) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_servicedir (char const *name)
{
  unsigned int n = resolve_service(name) ;
  if (n >= db->nlong)
    strerr_dief5x(1, "in database ", compiled, ": identifier ", name, " does not represent a longrun service") ;
  if (buffer_puts(buffer_1, db->string + db->services[n].x.longrun.servicedir) < 0
   || buffer_putflush(buffer_1, "\n", 1) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_deps (char const *name, int h)
{
  uint32 const *p ;
  unsigned int ndeps ;
  unsigned int n = resolve_service(name) ;
  if (n >= db->nshort + db->nlong)
    strerr_dief5x(1, "in database ", compiled, ": identifier ", name, " represents a bundle") ;
  p = db->deps + h * db->ndeps + db->services[n].deps[h] ;
  ndeps = db->services[n].ndeps[h] ;
  while (ndeps--)
    if (buffer_puts(buffer_1, db->string + db->services[*p++].name) < 0
     || buffer_put(buffer_1, "\n", 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_script (char const *name, int h)
{
  unsigned int argc ;
  char *const *argv ;
  unsigned int n = resolve_service(name) ;
  if (n >= db->nshort + db->nlong)
    strerr_dief5x(1, "in database ", compiled, ": identifier ", name, " represents a bundle") ;
  if (n < db->nlong)
    strerr_dief5x(1, "in database ", compiled, ": identifier ", name, " represents a longrun service") ;
  argv = db->argvs + db->services[n].x.oneshot.argv[h] ;
  argc = db->services[n].x.oneshot.argc[h] ;
  while (argc--)
    if (buffer_puts(buffer_1, *argv++) < 0
     || buffer_put(buffer_1, "\0", 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static inline void print_flags (char const *name)
{
  unsigned int n = resolve_service(name) ;
  char fmt[9] = "00000000\n" ;
  if (n >= db->nshort + db->nlong)
    strerr_dief5x(1, "in database ", compiled, ": identifier ", name, " represents a bundle") ;
  uint320_xfmt(fmt, db->services[n].flags, 8) ;
  if (buffer_putflush(buffer_1, fmt, 9) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_atomics (char const *const *argv, int h, int doclosure)
{
  unsigned int n = db->nshort + db->nlong ;
  cdb_t c = CDB_ZERO ;
  int fd = open_readatb(fdcompiled, "resolve.cdb") ;
  unsigned char state[n] ;
  if (fd < 0) strerr_diefu3sys(111, "open ", compiled, "/resolve.cdb") ;
  if (!cdb_init_map(&c, fd, 1))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  byte_zero(state, n) ;
  for (; *argv ; argv++)
  {
    register int r = cdb_find(&c, *argv, str_len(*argv)) ;
    if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/resolve.cdb") ;
    if (!r) strerr_dief3x(1, *argv, " is not a valid identifier in ", compiled) ;
    {
      unsigned int len = cdb_datalen(&c) >> 2 ;
      char pack[cdb_datalen(&c) + 1] ;
      register char const *p = pack ;
      if (cdb_read(&c, pack, cdb_datalen(&c), cdb_datapos(&c)) < 0)
        strerr_diefu3sys(111, "cdb_read ", compiled, "/resolve.cdb") ;
      while (len--)
      {
        uint32 x ;
        uint32_unpack_big(p, &x) ; p += 4 ;
        if (x >= db->nshort + db->nlong)
          strerr_dief2x(4, "invalid database in ", compiled) ;
        state[x] |= 1 ;
      }
    }
  }
  cdb_free(&c) ;
  close(fd) ;
  if (doclosure) s6rc_graph_closure(db, state, 0, h) ;
  while (n--) if (state[n])
    if (buffer_puts(buffer_1, db->string + db->services[n].name) < 0
     || buffer_put(buffer_1, "\n", 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static inline void print_help (void)
{
  static char const *help =
"s6-rc-db help\n"
"s6-rc-db check\n"
"s6-rc-db list all|services|oneshots|longruns|bundles\n"
"s6-rc-db type servicename\n"
"s6-rc-db [ -u | -d ] timeout atomicname\n"
"s6-rc-db contents bundlename\n"
"s6-rc-db [ -u | -d ] dependencies servicename\n"
"s6-rc-db servicedir longrunname\n"
"s6-rc-db [ -u | -d ] script oneshotname\n"
"s6-rc-db flags atomicname\n"
"s6-rc-db atomics servicename...\n"
"s6-rc-db [ -u | -d ] all-dependencies servicename...\n" ;
  if (buffer_putsflush(buffer_1, help) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static unsigned int lookup (char const *const *table, char const *command)
{
  register unsigned int i = 0 ;
  for (; table[i] ; i++) if (!str_diff(command, table[i])) break ;
  return i ;
}

static inline unsigned int parse_command (char const *command)
{
  static char const *const command_table[13] =
  {
    "help",
    "check",
    "list",
    "type",
    "timeout",
    "contents",
    "dependencies",
    "servicedir",
    "script",
    "flags",
    "atomics",
    "all-dependencies",
    0
  } ;
  register unsigned int i = lookup(command_table, command) ;
  if (!command_table[i]) dieusage() ;
  return i ;
}

static inline unsigned int parse_list (char const *command)
{
  static char const *const list_table[6] =
  {
    "all",
    "services",
    "oneshots",
    "longruns",
    "bundles",
    0
  } ;
  register unsigned int i = lookup(list_table, command) ;
  if (!list_table[i]) dieusage() ;
  return i ;
}

int main (int argc, char const *const *argv)
{
  char const *live = S6RC_LIVE_BASE ;
  unsigned int what, subwhat = 0 ;
  int up = 1 ;
  PROG = "s6-rc-db" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "udl:c:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'l' : live = l.arg ; break ;
        case 'c' : compiled = l.arg ; break ;
        case 'u' : up = 1 ; break ;
        case 'd' : up = 0 ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }

  if (!argc) dieusage() ;
  what = parse_command(argv[0]) ;
  if (!what)
  {
    print_help() ;
    return 0 ;
  }
  if (what >= 2 && argc < 2) dieusage() ;
  if (what == 2) subwhat = 1 + parse_list(argv[1]) ;

  {
    unsigned int livelen = str_len(live) ;
    s6rc_db_t dbblob ;
    char compiledblob[compiled ? str_len(compiled) : livelen + 10] ;
    db = &dbblob ;

    if (!compiled)
    {
      byte_copy(compiledblob, livelen, live) ;
      byte_copy(compiledblob + livelen, 10, "/compiled") ;
      compiled = compiledblob ;
    }

    fdcompiled = open_readb(compiled) ;
    if (fdcompiled < 0)
      strerr_diefu2sys(111, "open ", compiled) ;


   /* Read the sizes of the compiled db */

    fdcompiled = open_readb(compiled) ;
    if (!s6rc_db_read_sizes(fdcompiled, &dbblob))
      strerr_diefu3sys(111, "read ", compiled, "/n") ;


   /* Allocate enough stack for the db */

    {
      unsigned int n = dbblob.nshort + dbblob.nlong ;
      s6rc_service_t serviceblob[n] ;
      char *argvblob[dbblob.nargvs] ;
      uint32 depsblob[dbblob.ndeps << 1] ;
      char stringblob[dbblob.stringlen] ;
      register int r ;

      dbblob.services = serviceblob ;
      dbblob.argvs = argvblob ;
      dbblob.deps = depsblob ;
      dbblob.string = stringblob ;


     /* Read the db from the file */

      r = s6rc_db_read(fdcompiled, &dbblob) ;
      if (r < 0) strerr_diefu3sys(111, "read ", compiled, "/db") ;
      if (!r) strerr_dief3x(4, "invalid service database in ", compiled, "/db") ;


     /* Perform the action */

      switch (what)
      {
        case 1 : /* check */
        {
          unsigned int problem, w ;
          if (!s6rc_db_check_revdeps(&dbblob))
            strerr_dief3x(4, "invalid service database in ", compiled, ": direct and reverse dependencies are mismatched") ;
          w = s6rc_db_check_depcycles(&dbblob, 1, &problem) ;
          if (w < n) strerr_dief6x(4, "invalid service database in ", compiled, ": service ", stringblob + serviceblob[w].name, " has a dependency cycle involving ", stringblob + serviceblob[problem].name) ;
          break ;
        }
        case 2 : /* list */
          switch (subwhat)
          {
            case 1 : print_all(0) ; break ;
            case 2 : print_services(0, n) ; break ;
            case 3 : print_services(dbblob.nlong, n) ; break ;
            case 4 : print_services(0, dbblob.nlong) ; break ;
            case 5 : print_all(1) ; break ;
          }
          break ;
        case 3 : /* type */
          print_type(argv[1]) ;
          break ;
        case 4 : /* timeout */
          print_timeout(argv[1], up) ;
          break ;
        case 5 : /* contents */
          print_bundle_contents(argv[1]) ;
          break ;
        case 6 : /* dependencies */
          print_deps(argv[1], up) ;
          break ;
        case 7 : /* servicedir */
          print_servicedir(argv[1]) ;
          break ;
        case 8 : /* script */
          print_script(argv[1], up) ;
          break ;
        case 9 : /* flags */
          print_flags(argv[1]) ;
          break ;
        case 10 : /* atomics */
          print_atomics(argv + 1, 1, 0) ;
          break ;
        case 11 : /* all-dependencies */
          print_atomics(argv + 1, up, 1) ;
          break ;
      }
    }
  }
  return 0 ;
}
