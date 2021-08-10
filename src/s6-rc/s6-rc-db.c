/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <skalibs/uint32.h>
#include <skalibs/sgetopt.h>
#include <skalibs/buffer.h>
#include <skalibs/strerr2.h>
#include <skalibs/djbunix.h>
#include <skalibs/cdb.h>
#include <skalibs/unix-transactional.h>

#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-db [ -u | -d ] [ -l live ] [ -c compiled ] [ -b ] command... (use s6-rc-db help for more information)"
#define dieusage() strerr_dieusage(100, USAGE)

static char const *compiled = 0 ;
static int fdcompiled = -1 ;
static s6rc_db_t *db ;

static void print_bundle_contents (char const *name)
{
  cdb c = CDB_ZERO ;
  cdb_data data ;
  int r ;
  if (!cdb_init_at(&c, fdcompiled, "resolve.cdb"))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  r = cdb_find(&c, &data, name, strlen(name)) ;
  if (r < 0) strerr_dief3x(4, "invalid cdb in ", compiled, "/resolve.cdb") ;
  if (!r) strerr_dief3x(3, name, " is not a valid identifier in ", compiled) ;
  if (data.len == 4)
  {
    uint32_t x ;
    uint32_unpack_big(data.s, &x) ;
    if (x >= db->nshort + db->nlong)
      strerr_dief2x(4, "invalid database in ", compiled) ;
    if (!strcmp(name, db->string + db->services[x].name))
      strerr_dief5x(5, "in database ", compiled, ": identifier ", name, " represents an atomic service") ;
    if (buffer_puts(buffer_1, db->string + db->services[x].name) < 0
     || buffer_put(buffer_1, "\n", 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  }
  else
  {
    if (data.len & 3)
      strerr_dief2x(4, "invalid database in ", compiled) ;
    while (data.len)
    {
      uint32_t x ;
      uint32_unpack_big(data.s, &x) ; data.s += 4 ; data.len -= 4 ;
      if (x >= db->nshort + db->nlong)
        strerr_dief2x(4, "invalid database in ", compiled) ;
      if (buffer_puts(buffer_1, db->string + db->services[x].name) < 0
       || buffer_put(buffer_1, "\n", 1) < 1)
        strerr_diefu1sys(111, "write to stdout") ;
    }
  }
  cdb_free(&c) ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_services (unsigned int from, unsigned int to)
{
  for (; from < to ; from++)
  {
    if (buffer_puts(buffer_1, db->string + db->services[from].name) < 0
     || buffer_put(buffer_1, "\n", 1) < 1)
      strerr_diefu1sys(111, "write to stdout") ;
  }
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_all (int bundlesonly)
{
  cdb c = CDB_ZERO ;
  uint32_t pos = CDB_TRAVERSE_INIT() ;
  if (!cdb_init_at(&c, fdcompiled, "resolve.cdb"))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  for (;;)
  {
    cdb_data key, data ;
    int r = cdb_traverse_next(&c, &key, &data, &pos) ;
    if (r < 0) strerr_dief3x(4, "invalid cdb in ", compiled, "/resolve.cdb") ;
    if (!r) break ;
    if (bundlesonly && data.len == 4)
    {
      uint32_t x ;
      uint32_unpack_big(data.s, &x) ;
      if (x >= db->nshort + db->nlong)
        strerr_dief2x(4, "invalid database in ", compiled) ;
      if (!strncmp(key.s, db->string + db->services[x].name, key.len)
       && !db->string[db->services[x].name + key.len])
        continue ;
    }
    if (buffer_put(buffer_1, key.s, key.len) < 0
     || buffer_put(buffer_1, "\n", 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  }
  cdb_free(&c) ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static unsigned int resolve_service (char const *name)
{
  cdb c = CDB_ZERO ;
  cdb_data data ;
  uint32_t x ;
  int r ;
  if (!cdb_init_at(&c, fdcompiled, "resolve.cdb"))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  r = cdb_find(&c, &data, name, strlen(name)) ;
  if (r < 0) strerr_dief3x(4, "invalid cdb in ", compiled, "/resolve.cdb") ;
  if (!r) strerr_dief3x(3, name, " is not a valid identifier in ", compiled) ;
  if (data.len != 4) return db->nshort + db->nlong ;
  uint32_unpack_big(data.s, &x) ;
  cdb_free(&c) ;
  if (x >= db->nshort + db->nlong)
    strerr_dief2x(4, "invalid database in ", compiled) ;
  if (strcmp(name, db->string + db->services[x].name))
    x = db->nshort + db->nlong ;
  return x ;
}

static void print_type (char const *name)
{
  unsigned int n = resolve_service(name) ;
  char const *s = n >= db->nshort + db->nlong ? "bundle" : n < db->nlong ? "longrun" : "oneshot" ;
  if (buffer_puts(buffer_1, s) < 0
   || buffer_putflush(buffer_1, "\n", 1) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_timeout (char const *name, int h)
{
  size_t len ;
  unsigned int n = resolve_service(name) ;
  char fmt[UINT32_FMT] ;
  if (n >= db->nshort + db->nlong)
    strerr_dief5x(5, "in database ", compiled, ": identifier ", name, " represents a bundle") ;
  len = uint32_fmt(fmt, db->services[n].timeout[h]) ;
  fmt[len++] = '\n' ;
  if (buffer_putflush(buffer_1, fmt, len) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_script (char const *name, int h)
{
  unsigned int argc ;
  char const *const *argv ;
  unsigned int n = resolve_service(name) ;
  if (n >= db->nshort + db->nlong)
    strerr_dief5x(5, "in database ", compiled, ": identifier ", name, " represents a bundle") ;
  if (n < db->nlong)
    strerr_dief5x(5, "in database ", compiled, ": identifier ", name, " represents a longrun service") ;
  argv = db->argvs + db->services[n].x.oneshot.argv[h] ;
  argc = db->services[n].x.oneshot.argc[h] ;
  while (argc--)
    if (buffer_puts(buffer_1, *argv++) < 0
     || buffer_put(buffer_1, "\0", 1) < 1)
      strerr_diefu1sys(111, "write to stdout") ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_producers_rec (uint32_t n)
{
  uint32_t i = 0 ;
  for (; i < db->services[n].x.longrun.nproducers ; i++)
  {
    uint32_t m = db->producers[db->services[n].x.longrun.producers + i] ;
    print_producers_rec(m) ;
    if (buffer_puts(buffer_1, db->string + db->services[m].name) < 0
     || buffer_put(buffer_1, " | ", 3) < 0
     || buffer_puts(buffer_1, db->string + db->services[n].name) < 0
     || buffer_put(buffer_1, "\n", 1) < 0)
      strerr_diefu1sys(111, "write to stdout") ;
  }
}

static inline void print_pipeline (char const *name)
{
  unsigned int n = resolve_service(name) ;
  if (n >= db->nlong)
    strerr_dief5x(5, "in database ", compiled, ": identifier ", name, " does not represent a longrun") ;
  for (;;)
  {
    uint32_t j = db->services[n].x.longrun.consumer ;
    if (j >= db->nlong) break ;
    n = j ;
  }
  print_producers_rec(n) ;
  if (!buffer_flush(buffer_1))
    strerr_diefu1sys(111, "write to stdout") ;
}

static inline void print_flags (char const *name)
{
  unsigned int n = resolve_service(name) ;
  char fmt[9] = "00000000\n" ;
  if (n >= db->nshort + db->nlong)
    strerr_dief5x(5, "in database ", compiled, ": identifier ", name, " represents a bundle") ;
  uint320_xfmt(fmt, db->services[n].flags, 8) ;
  if (buffer_putflush(buffer_1, fmt, 9) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static void print_union (char const *const *argv, int h, int isdeps, int doclosure)
{
  unsigned int n = db->nshort + db->nlong ;
  cdb c = CDB_ZERO ;
  unsigned char state[n] ;
  if (!cdb_init_at(&c, fdcompiled, "resolve.cdb"))
    strerr_diefu3sys(111, "cdb_init ", compiled, "/resolve.cdb") ;
  memset(state, 0, n) ;
  for (; *argv ; argv++)
  {
    cdb_data data ;
    int r = cdb_find(&c, &data, *argv, strlen(*argv)) ;
    if (r < 0) strerr_dief3x(4, "invalid cdb in ", compiled, "/resolve.cdb") ;
    if (!r) strerr_dief3x(3, *argv, " is not a valid identifier in ", compiled) ;
    {
      if (data.len & 3) strerr_dief2x(4, "invalid database in ", compiled) ;
      while (data.len)
      {
        uint32_t x ;
        uint32_unpack_big(data.s, &x) ; data.s += 4 ; data.len -= 4 ;
        if (x >= db->nshort + db->nlong)
          strerr_dief2x(4, "invalid database in ", compiled) ;
        if (isdeps)
        {
          uint32_t ndeps = db->services[x].ndeps[h] ;
          uint32_t const *deps = db->deps + h * db->ndeps + db->services[x].deps[h] ;
          while (ndeps--) state[*deps++] |= 1 ;
        }
        else state[x] |= 1 ;
      }
    }
  }
  cdb_free(&c) ;
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
"s6-rc-db [ -u | -d ] dependencies servicename...\n"
"s6-rc-db pipeline longrunname\n"
"s6-rc-db [ -u | -d ] script oneshotname\n"
"s6-rc-db flags atomicname\n"
"s6-rc-db atomics servicename...\n"
"s6-rc-db [ -u | -d ] all-dependencies servicename...\n" ;
  if (buffer_putsflush(buffer_1, help) < 0)
    strerr_diefu1sys(111, "write to stdout") ;
}

static unsigned int lookup (char const *const *table, char const *command)
{
  unsigned int i = 0 ;
  for (; table[i] ; i++) if (!strcmp(command, table[i])) break ;
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
    "pipeline",
    "script",
    "flags",
    "atomics",
    "all-dependencies",
    0
  } ;
  unsigned int i = lookup(command_table, command) ;
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
  unsigned int i = lookup(list_table, command) ;
  if (!list_table[i]) dieusage() ;
  return i ;
}

int main (int argc, char const *const *argv)
{
  char const *live = S6RC_LIVE_BASE ;
  unsigned int what, subwhat = 0 ;
  int up = 1 ;
  int blocking = 0 ;
  PROG = "s6-rc-db" ;
  {
    subgetopt l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "udl:c:b", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'l' : live = l.arg ; break ;
        case 'c' : compiled = l.arg ; break ;
        case 'u' : up = 1 ; break ;
        case 'd' : up = 0 ; break ;
        case 'b' : blocking = 1 ; break ;
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
    size_t livelen = strlen(live) ;
    int compiledlock ;
    s6rc_db_t dbblob ;
    char compiledblob[compiled ? strlen(compiled) : livelen + 10] ;
    db = &dbblob ;

    if (!compiled)
    {
      memcpy(compiledblob, live, livelen) ;
      memcpy(compiledblob + livelen, "/compiled", 10) ;
      compiled = compiledblob ;
    }

    if (!s6rc_lock(0, 0, 0, compiled, 1, &compiledlock, blocking))
      strerr_diefu2sys(111, "take lock on ", compiled) ;
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
      char const *argvblob[dbblob.nargvs] ;
      uint32_t depsblob[dbblob.ndeps << 1] ;
      uint32_t producersblob[dbblob.nproducers] ;
      char stringblob[dbblob.stringlen] ;
      int r ;

      dbblob.services = serviceblob ;
      dbblob.argvs = argvblob ;
      dbblob.deps = depsblob ;
      dbblob.producers = producersblob ;
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
          diuint32 problem ;
          if (s6rc_db_check_revdeps(&dbblob))
            strerr_dief3x(4, "invalid service database in ", compiled, ": direct and reverse dependencies are mismatched") ;
          if (s6rc_db_check_depcycles(&dbblob, 1, &problem))
            strerr_dief8x(4, "invalid service database in ", compiled, ": dependency ", "cycle", " reached from ", stringblob + serviceblob[problem.left].name, " and involving ", stringblob + serviceblob[problem.right].name) ;
          r = s6rc_db_check_pipelines(&dbblob, &problem) ;
          if (r)
          {
            if (r == 1)
              strerr_dief8x(4, "invalid service database in ", compiled, ": pipeline ", "cycle", " reached from ", stringblob + serviceblob[problem.left].name, " and involving ", stringblob + serviceblob[problem.right].name) ;
            else
              strerr_dief8x(4, "invalid service database in ", compiled, ": pipeline ", "collision", " reached from ", stringblob + serviceblob[problem.left].name, " and involving ", stringblob + serviceblob[problem.right].name) ;
          }
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
          print_union(argv + 1, up, 1, 0) ;
          break ;
        case 7 : /* pipeline */
          print_pipeline(argv[1]) ;
          break ;
        case 8 : /* script */
          print_script(argv[1], up) ;
          break ;
        case 9 : /* flags */
          print_flags(argv[1]) ;
          break ;
        case 10 : /* atomics */
          print_union(argv + 1, 1, 0, 0) ;
          break ;
        case 11 : /* all-dependencies */
          print_union(argv + 1, up, 0, 1) ;
          break ;
      }
    }
  }
  return 0 ;
}
