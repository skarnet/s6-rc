/* ISC license. */

/* for fdopendir() on BSD. Fuck you, BSD. */
#include <skalibs/nonposix.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <skalibs/uint32.h>
#include <skalibs/uint.h>
#include <skalibs/bytestr.h>
#include <skalibs/bitarray.h>
#include <skalibs/strerr2.h>
#include <skalibs/sgetopt.h>
#include <skalibs/allreadwrite.h>
#include <skalibs/buffer.h>
#include <skalibs/cdb_make.h>
#include <skalibs/direntry.h>
#include <skalibs/djbunix.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/skamisc.h>
#include <skalibs/avltree.h>
#include <skalibs/unix-transactional.h>
#include <execline/config.h>
#include <execline/execline.h>
#include <s6/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-compile [ -v verbosity ] destdir sources..."
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_dief1x(111, "out of memory") ;

#define S6RC_ONESHOT_RUNNER_RUNSCRIPT \
"#!" EXECLINE_EXTBINPREFIX "execlineb -P\n" \
EXECLINE_EXTBINPREFIX "fdmove -c 2 1\n" \
EXECLINE_EXTBINPREFIX "fdmove 1 3\n" \
S6_EXTBINPREFIX "s6-ipcserver-socketbinder -- s\n" \
S6_EXTBINPREFIX "s6-ipcserverd -1 --\n" \
S6_EXTBINPREFIX "s6-ipcserver-access -v0 -E -l0 -i data/rules --\n" \
S6_EXTBINPREFIX "s6-sudod -t 2000 --\n"

static unsigned int verbosity = 1 ;
static stralloc keep = STRALLOC_ZERO ;
static stralloc data = STRALLOC_ZERO ;

typedef enum servicetype_e servicetype_t, *servicetype_t_ref ;
enum servicetype_e
{
  SVTYPE_UNDEFINED = 0,
  SVTYPE_ONESHOT = 1,
  SVTYPE_LONGRUN = 2,
  SVTYPE_BUNDLE = 4
} ;



 /* The names tree */


typedef struct nameinfo_s nameinfo_t, *nameinfo_t_ref ;
struct nameinfo_s
{
  unsigned int pos ;
  unsigned int kpos ;
  unsigned int i ;
  servicetype_t type ;
} ;

static void *names_dtok (unsigned int d, void *x)
{
  return (void *)(data.s + genalloc_s(nameinfo_t, (genalloc *)x)[d].pos) ;
}

static int names_cmp (void const *a, void const *b, void *x)
{
  (void)x ;
  return str_diff((char const *)a, (char const *)b) ;
}

static genalloc nameinfo = GENALLOC_ZERO ; /* nameinfo_t */
static avltree names_map = AVLTREE_INIT(8, 3, 8, &names_dtok, &names_cmp, &nameinfo) ;



 /* Services before resolution */


typedef struct common_s common_t, *common_t_ref ;
struct common_s
{
  unsigned int name ; /* pos in data */
  unsigned int kname ; /* pos in keep */
  unsigned int ndeps ;
  unsigned int depindex ; /* pos in indices */
  uint32 annotation_flags ;
  uint32 timeout[2] ;
} ;

typedef struct oneshot_s oneshot_t, *oneshot_t_ref ;
struct oneshot_s
{
  common_t common ;
  unsigned int argvindex[2] ; /* pos in data */
  unsigned int argc[2] ;
} ;

typedef struct longrun_s longrun_t, *longrun_t_ref ;
struct longrun_s
{
  common_t common ;
  char const *srcdir ;
  unsigned int servicedirname ;
  unsigned int logrelated ;
  unsigned char logtype ;
} ;

typedef struct bundle_s bundle_t, *bundle_t_ref ;
struct bundle_s
{
  unsigned int name ; /* pos in data */
  unsigned int n ;
  unsigned int listindex ; /* pos in indices */
} ;

typedef struct before_s before_t, *before_t_ref ;
struct before_s
{
  genalloc indices ; /* uint32 */
  genalloc oneshots ; /* oneshot_t */
  genalloc longruns ; /* longrun_t */
  genalloc bundles ; /* bundle_t */
  unsigned int nargvs ;
  unsigned int maxnamelen ;
} ;

#define BEFORE_ZERO { .indices = GENALLOC_ZERO, .oneshots = GENALLOC_ZERO, .longruns = GENALLOC_ZERO, .bundles = GENALLOC_ZERO, .nargvs = 0 } ;



 /* Read all the sources, populate the map and string data */


static char const *typestr (servicetype_t type)
{
  return (type == SVTYPE_ONESHOT) ? "oneshot" :
         (type == SVTYPE_LONGRUN) ? "longrun" :
         (type == SVTYPE_BUNDLE) ? "bundle" :
         "unknown" ;
}

static int add_name (before_t *be, char const *srcdir, char const *name, servicetype_t type, unsigned int *pos, unsigned int *kpos)
{
  unsigned int id ;

  if (verbosity >= 4)
    strerr_warnt6x("from ", srcdir, ": adding identifier ", name, " of type ", typestr(type)) ;

  if (avltree_search(&names_map, name, &id))
  {
    nameinfo_t *info = genalloc_s(nameinfo_t, &nameinfo) + id ;
    if (type == SVTYPE_UNDEFINED)
    {
      if (verbosity >= 4)
        strerr_warnt4x("identifier ", name, " was already declared with type ", typestr(info->type)) ;
      switch (info->type)
      {
        case SVTYPE_ONESHOT :
        case SVTYPE_LONGRUN : *kpos = info->kpos ;
        default : break ;
      }
    }
    else if (info->type == SVTYPE_UNDEFINED)
    {
      info->type = type ;
      if (verbosity >= 4)
        strerr_warnt4x("previously encountered identifier ", name, " has now type ", typestr(type)) ;
      switch (type)
      {
        case SVTYPE_UNDEFINED : break ;
        case SVTYPE_ONESHOT :
          info->i = genalloc_len(oneshot_t, &be->oneshots) ;
          info->kpos = keep.len ;
          if (!stralloc_cats(&keep, name) || !stralloc_0(&keep)) dienomem() ;
          break ;
        case SVTYPE_LONGRUN :
          info->i = genalloc_len(longrun_t, &be->longruns) ;
          info->kpos = keep.len ;
          if (!stralloc_cats(&keep, name) || !stralloc_0(&keep)) dienomem() ;
          break ;
        case SVTYPE_BUNDLE :
          info->i = genalloc_len(bundle_t, &be->bundles) ;
          break ;
      }
    }
    else strerr_dief6x(1, "duplicate service definition: ", name, " defined in ", srcdir, " is already defined and has type ", typestr(info->type)) ;
    *pos = info->pos ;
    *kpos = info->kpos ;
    return 1 ;
  }
  else
  {
    nameinfo_t info =
    {
      .pos = data.len,
      .kpos = keep.len,
      .i = type == SVTYPE_ONESHOT ? genalloc_len(oneshot_t, &be->oneshots) :
           type == SVTYPE_LONGRUN ? genalloc_len(longrun_t, &be->longruns) :
           type == SVTYPE_BUNDLE ? genalloc_len(bundle_t, &be->bundles) :
           0,
      .type = type
    } ;
    unsigned int namelen = str_len(name) ;
    unsigned int i = genalloc_len(nameinfo_t, &nameinfo) ;
    if (!stralloc_catb(&data, name, namelen + 1)) dienomem() ;
    if (type == SVTYPE_ONESHOT || type == SVTYPE_LONGRUN)
      if (!stralloc_catb(&keep, name, namelen + 1)) dienomem() ;
    if (!genalloc_append(nameinfo_t, &nameinfo, &info)) dienomem() ;
    if (!avltree_insert(&names_map, i)) dienomem() ;
    *pos = info.pos ;
    *kpos = info.kpos ;
    if (namelen > be->maxnamelen) be->maxnamelen = namelen ;
    return 0 ;
  }
}

static inline void add_specials (before_t *be)
{
  longrun_t service =
  {
    .common =
    {
      .ndeps = 0,
      .depindex = genalloc_len(unsigned int, &be->indices),
      .annotation_flags = 0,
      .timeout = { 0, 0 }
    },
    .srcdir = 0,
    .servicedirname = keep.len,
    .logrelated = 0,
    .logtype = 0
  } ;
  add_name(be, "(s6-rc-compile internals)", S6RC_ONESHOT_RUNNER, SVTYPE_LONGRUN, &service.common.name, &service.common.kname) ;
  if (!stralloc_cats(&keep, data.s + service.common.name)
   || !stralloc_0(&keep)) dienomem() ;
  if (!genalloc_append(longrun_t, &be->longruns, &service)) dienomem() ;
}

static int uint_uniq (unsigned int const *list, unsigned int n, unsigned int pos)
{
  while (n--) if (pos == list[n]) return 0 ;
  return 1 ;
}

static int add_namelist (before_t *be, int dirfd, char const *srcdir, char const *name, char const *list, unsigned int *listindex, unsigned int *n)
{
  buffer b ;
  unsigned int start = satmp.len ;
  int cont = 1 ;
  char buf[2048] ;
  int fd = open_readatb(dirfd, list) ;
  if (fd < 0) return 0 ;
  buffer_init(&b, &fd_readsv, fd, buf, 2048) ;
  *listindex = genalloc_len(unsigned int, &be->indices) ;
  while (cont)
  {
    register int r = skagetln(&b, &satmp, '\n') ;
    if (!r) cont = 0 ;
    else
    {
      unsigned int i = start ;
      if (r < 0)
      {
        if (errno != EPIPE) strerr_diefu6sys(111, "read ", srcdir, "/", name, "/", list) ;
        if (!stralloc_0(&satmp)) dienomem() ;
        cont = 0 ;
      }
      else satmp.s[satmp.len-1] = 0 ;
      while (satmp.s[i] == ' ' || satmp.s[i] == '\t') i++ ;
      if (satmp.s[i] && satmp.s[i] != '#')
      {
        unsigned int pos, kpos ;
        add_name(be, name, satmp.s + i, SVTYPE_UNDEFINED, &pos, &kpos) ;
        if (uint_uniq(genalloc_s(unsigned int, &be->indices) + *listindex, genalloc_len(unsigned int, &be->indices) - *listindex, pos))
        {
          if (!genalloc_append(unsigned int, &be->indices, &pos))
            dienomem() ;
        }
        else if (verbosity >= 2)
          strerr_warnw8x("in ", srcdir, "/", name, "/", list, ": name listed more than once: ", satmp.s + i) ;
      }
      satmp.len = start ;
    }
  }
  close(fd) ;
  *n = genalloc_len(unsigned int, &be->indices) - *listindex ;
  return 1 ;
}

static void read_script (before_t *be, int dirfd, char const *srcdir, char const *name, char const *script, unsigned int *argvindex, unsigned int *argc)
{
  buffer b ;
  int r ;
  char buf[4096] ;
  int fd = open_readatb(dirfd, script) ;
  if (fd < 0) strerr_diefu6sys(111, "open ", srcdir, "/", name, "/", script) ;
  buffer_init(&b, &fd_readsv, fd, buf, 4096) ;
  *argvindex = keep.len ;
  r = el_parse_from_buffer(&keep, &b) ;
  switch (r)
  {
    case -3 : strerr_dief7x(1, "syntax error in ", srcdir, "/", name, "/", script, ": missing }");
    case -2 : strerr_dief6x(1, "syntax error in ", srcdir, "/", name, "/", script) ;
    case -1 : strerr_diefu6sys(111, "parse ", srcdir, "/", name, "/", script) ;
  }
  close(fd) ;
  *argc = r ;
  be->nargvs += r+1 ;
}

static uint32 read_timeout (int dirfd, char const *srcdir, char const *name, char const *tname)
{
  char buf[64] ;
  uint32 timeout = 0 ;
  unsigned int r = openreadnclose_at(dirfd, tname, buf, 63) ;
  if (!r)
  {
    if (errno && errno != ENOENT)
      strerr_diefu6sys(111, "read ", srcdir, "/", name, "/", tname) ;
  }
  else
  {
    while (r && buf[r-1] == '\n') r-- ;
    buf[r] = 0 ;
    if (!uint320_scan(buf, &timeout))
      strerr_dief6x(1, "invalid ", srcdir, "/", name, "/", tname) ;
  }
  return timeout ;
}

static void add_common (before_t *be, int dirfd, char const *srcdir, char const *name, common_t *common, servicetype_t svtype)
{
  common->annotation_flags = 0 ;
  add_name(be, srcdir, name, svtype, &common->name, &common->kname) ;
  if (!add_namelist(be, dirfd, srcdir, name, "dependencies", &common->depindex, &common->ndeps))
  {
    if (errno != ENOENT)
      strerr_diefu5sys(111, "open ", srcdir, "/", name, "/dependencies") ;
    common->depindex = genalloc_len(unsigned int, &be->indices) ;
    common->ndeps = 0 ;
  }
  common->timeout[0] = read_timeout(dirfd, srcdir, name, "timeout-down") ;
  common->timeout[1] = read_timeout(dirfd, srcdir, name, "timeout-up") ;
}

static inline void add_oneshot (before_t *be, int dirfd, char const *srcdir, char const *name)
{
  static unsigned int const special_dep = 0 ;
  oneshot_t service ;
  if (verbosity >= 3) strerr_warni3x(name, " has type ", "oneshot") ;
  add_common(be, dirfd, srcdir, name, &service.common, SVTYPE_ONESHOT) ;
  read_script(be, dirfd, srcdir, name, "up", &service.argvindex[1], &service.argc[1]) ;
  read_script(be, dirfd, srcdir, name, "down", &service.argvindex[0], &service.argc[0]) ;
  if (uint_uniq(genalloc_s(unsigned int, &be->indices) + service.common.depindex, service.common.ndeps, 0))
  {
    if (!genalloc_append(unsigned int, &be->indices, &special_dep)) dienomem() ;
    service.common.ndeps++ ;
  }
  else if (verbosity)
    strerr_warnw6x(srcdir, "/", name, "/dependencies", " explicitly lists ", S6RC_ONESHOT_RUNNER) ;
  if (verbosity >= 4)
  {
    unsigned int i = service.common.ndeps ;
    unsigned int const *p = genalloc_s(unsigned int const, &be->indices) + service.common.depindex ;
    char fmt[UINT_FMT] ;
    while (i--)
    {
      fmt[uint_fmt(fmt, *p)] = 0 ;
      strerr_warnt7x("dependency from ", name, " to ", data.s + *p, " (", fmt, ")") ;
      p++ ;
    }
    
  }
  if (!genalloc_append(oneshot_t, &be->oneshots, &service)) dienomem() ;
}

static inline void add_longrun (before_t *be, int dirfd, char const *srcdir, char const *name)
{
  longrun_t service = { .srcdir = srcdir } ;
  int fd ;
  unsigned int logindex, n ;
  if (verbosity >= 3) strerr_warni3x(name, " has type ", "longrun") ;
  add_common(be, dirfd, srcdir, name, &service.common, SVTYPE_LONGRUN) ;
  fd = open_readat(dirfd, "run") ;
  if (fd < 0)
  {
    if (errno == ENOENT)
      strerr_diefu5x(1, "find a run script in ", srcdir, "/", name, "/run") ;
    else strerr_diefu5sys(111, "open ", srcdir, "/", name, "/run") ;
  }
  else
  {
    struct stat st ;
    if (fstat(fd, &st) < 0)
      strerr_diefu5sys(111, "stat ", srcdir, "/", name, "/run") ;
    if (!S_ISREG(st.st_mode))
      strerr_dief4x(1, srcdir, "/", name, "/run is not a regular file") ;
  }
  close(fd) ;
  if (add_namelist(be, dirfd, srcdir, name, "logger", &logindex, &n))
  {
    register unsigned int const *deps = genalloc_s(unsigned int, &be->indices) + service.common.depindex ;
    register unsigned int i = 0 ;
    if (n != 1)
      strerr_dief5x(1, srcdir, "/", name, "/logger", " should only contain one service name") ;
    service.logtype = 1 ;
    service.logrelated = genalloc_s(unsigned int, &be->indices)[logindex] ;
    service.servicedirname = service.common.kname ;
    for (; i < service.common.ndeps ; i++) if (service.logrelated == deps[i]) break ;
    if (i < service.common.ndeps)
      genalloc_setlen(unsigned int, &be->indices, logindex) ;
    else service.common.ndeps++ ;
    if (verbosity >= 3)
      strerr_warni3x(name, " is a producer for ", data.s + service.logrelated) ;
  }
  else if (add_namelist(be, dirfd, srcdir, name, "producer", &logindex, &n))
  {
    if (n != 1)
      strerr_dief5x(1, srcdir, "/", name, "/producer", " should only contain one service name") ;
    service.logtype = 2 ;
    service.logrelated = genalloc_s(unsigned int, &be->indices)[logindex] ;
    genalloc_setlen(unsigned int, &be->indices, logindex) ;
    service.servicedirname = keep.len ;
    if (!stralloc_cats(&keep, data.s + service.logrelated)
     || !stralloc_catb(&keep, "/log", 5)) dienomem() ;
    if (verbosity >= 3)
      strerr_warni3x(name, " is a logger for ", data.s + service.logrelated) ;
  }
  else
  {
    service.logtype = 0 ;
    service.servicedirname = service.common.kname ;
    if (verbosity >= 3) strerr_warni2x(name, " has no logger or producer") ;
  }
  if (!genalloc_append(longrun_t, &be->longruns, &service)) dienomem() ;
}

static inline void add_bundle (before_t *be, int dirfd, char const *srcdir, char const *name)
{
  bundle_t bundle ;
  unsigned int dummy ;
  if (verbosity >= 3) strerr_warni3x(name, " has type ", "bundle") ;
  add_name(be, srcdir, name, SVTYPE_BUNDLE, &bundle.name, &dummy) ;
  if (!add_namelist(be, dirfd, srcdir, name, "contents", &bundle.listindex, &bundle.n))
    strerr_diefu5sys(111, "open ", srcdir, "/", name, "/contents") ;
  if (!genalloc_append(bundle_t, &be->bundles, &bundle)) dienomem() ;
}

static inline void add_source (before_t *be, int dirfd, char const *srcdir, char const *name)
{
  char typestr[8] = "" ;
  register unsigned int r ;
  if (verbosity >= 2) strerr_warni4x("parsing ", srcdir, "/", name) ;
  r = openreadnclose_at(dirfd, "type", typestr, 8) ;
  if (!r)
  {
    if (!errno) errno = EINVAL ;
    strerr_diefu5sys(111, "read ", srcdir, "/", name, "/type") ;
  }
  if (typestr[r-1] == '\n') r-- ;
  typestr[r++] = 0 ;
  if (!str_diff(typestr, "oneshot")) add_oneshot(be, dirfd, srcdir, name) ;
  else if (!str_diff(typestr, "longrun")) add_longrun(be, dirfd, srcdir, name) ;
  else if (!str_diff(typestr, "bundle")) add_bundle(be, dirfd, srcdir, name) ;
  else strerr_dief6x(1, "invalid ", srcdir, "/", name, "/type", ": must be oneshot, longrun, or bundle") ;
}

static inline void add_sources (before_t *be, char const *srcdir)
{
  DIR *dir ;
  int fddir = open_readb(srcdir) ;
  if (fddir < 0) strerr_diefu2sys(111, "open ", srcdir) ;
  dir = fdopendir(fddir) ;
  if (!dir) strerr_diefu2sys(111, "fdopendir ", srcdir) ;
  for (;;)
  {
    struct stat st ;
    int fd ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    if (lstat_at(fddir, d->d_name, &st) < 0)
      strerr_diefu4sys(111, "lstat ", srcdir, "/", d->d_name) ;
    if (!S_ISDIR(st.st_mode)) continue ;
    if (d->d_name[str_chr(d->d_name, '\n')])
      strerr_dief3x(2, "subdirectory of ", srcdir, " contains a newline character") ;
    fd = open_readatb(fddir, d->d_name) ;
    if (fd < 0) strerr_diefu4sys(111, "open ", srcdir, "/", d->d_name) ;
    add_source(be, fd, srcdir, d->d_name) ;
    close(fd) ;
  }
  if (errno) strerr_diefu2sys(111, "readdir ", srcdir) ;
  dir_close(dir) ;
}



 /* Resolve all names and dependencies */


typedef struct bundle_recinfo_s bundle_recinfo_t, *bundle_recinfo_t_ref ;
struct bundle_recinfo_s
{
  bundle_t const *oldb ;
  bundle_t *newb ;
  unsigned int n ;
  unsigned int nlong ;
  unsigned int nbits ;
  uint32 const *indices ;
  unsigned char *barray ;
  unsigned char *mark ;
  unsigned int source ;
} ;

static void resolve_bundle_rec (bundle_recinfo_t *recinfo, unsigned int i)
{
  if (!(recinfo->mark[i] & 2))
  {
    bundle_t const *me = recinfo->oldb + i ;
    uint32 const *listindex = recinfo->indices + me->listindex ;
    unsigned int j = 0 ;
    if (recinfo->mark[i] & 1)
      strerr_dief4x(1, "cyclic bundle definition: resolution of ", data.s + recinfo->oldb[recinfo->source].name, " encountered a cycle involving ", data.s + me->name) ;
    recinfo->mark[i] |= 1 ;
    for (; j < me->n ; j++)
    {
      unsigned int id ;
      register nameinfo_t const *p ;
      avltree_search(&names_map, data.s + listindex[j], &id) ;
      p = genalloc_s(nameinfo_t, &nameinfo) + id ;
      switch (p->type)
      {
        case SVTYPE_ONESHOT :
          bitarray_set(recinfo->barray + i * recinfo->nbits, recinfo->nlong + p->i) ;
          break ;
        case SVTYPE_LONGRUN :
          bitarray_set(recinfo->barray + i * recinfo->nbits, p->i) ;
          break ;
        case SVTYPE_BUNDLE :
          resolve_bundle_rec(recinfo, p->i) ;
          bitarray_or(recinfo->barray + i * recinfo->nbits, recinfo->barray + i * recinfo->nbits, recinfo->barray + p->i * recinfo->nbits, recinfo->n) ;
          break ;
        default :
          strerr_dief4x(1, "during resolution of bundle ", data.s + me->name, ": undefined service name ", data.s + p->pos) ;
      }
    }    
    recinfo->mark[i] |= 2 ;
  }
}

static inline unsigned int resolve_bundles (bundle_t const *oldb, bundle_t *newb, unsigned int nshort, unsigned int nlong, unsigned int nbundles, uint32 const *indices, unsigned char *barray)
{
  unsigned int total = 0, i = 0 ;
  unsigned char mark[nbundles] ;
  bundle_recinfo_t recinfo = { .oldb = oldb, .newb = newb, .n = nshort + nlong, .nlong = nlong, .nbits = bitarray_div8(nshort + nlong), .indices = indices, .barray = barray, .mark = mark } ;
  if (verbosity >= 2) strerr_warni1x("resolving bundle names") ;
  byte_zero(barray, recinfo.nbits * nbundles) ;
  byte_zero(mark, nbundles) ;
  for (; i < nbundles ; i++)
  {
    newb[i].name = oldb[i].name ;
    recinfo.source = i ;
    resolve_bundle_rec(&recinfo, i) ;
  }
  for (i = 0 ; i < nbundles ; i++)
  {
    newb[i].listindex = total ;
    newb[i].n = bitarray_countones(barray + i * recinfo.nbits, recinfo.n) ;
    total += newb[i].n ;
  }
  return total ;
}

static inline void flatlist_bundles (bundle_t *bundles, unsigned int nbundles, unsigned int nbits, unsigned char const *barray, uint32 *bdeps)
{
  unsigned int i = 0 ;
  if (verbosity >= 3) strerr_warni1x("converting bundle array") ;
  for (; i < nbundles ; i++)
  {
    unsigned char const *mybits = barray + i * nbits ;
    uint32 *mydeps = bdeps + bundles[i].listindex ;
    unsigned int j = 0, k = 0 ;
    for (; k < bundles[i].n ; j++)
      if (bitarray_peek(mybits, j)) mydeps[k++] = j ;
  }
}

static void resolve_deps (common_t const *me, unsigned int nlong, unsigned int n, unsigned int nbits, uint32 const *indices, unsigned char *sarray, unsigned char const *barray)
{
  unsigned int j = 0 ;
  for (; j < me->ndeps ; j++)
  {
    unsigned int id ;
    register nameinfo_t const *p ;
    avltree_search(&names_map, data.s + indices[me->depindex + j], &id) ;
    p = genalloc_s(nameinfo_t, &nameinfo) + id ;
    switch (p->type)
    {
      case SVTYPE_ONESHOT :
        bitarray_set(sarray, nlong + p->i) ;
        if (verbosity >= 4)
        {
          char fmt[UINT_FMT] ;
          fmt[uint_fmt(fmt, nlong + p->i)] = 0 ;
          strerr_warnt7x("atomic ", data.s + me->name, " depends on oneshot ", data.s + p->pos, " (", fmt, ")") ;
        }
        break ;
      case SVTYPE_LONGRUN :
        bitarray_set(sarray, p->i) ;
        if (verbosity >= 4)
        {
          char fmt[UINT_FMT] ;
          fmt[uint_fmt(fmt, p->i)] = 0 ;
          strerr_warnt7x("atomic ", data.s + me->name, " depends on longrun ", data.s + p->pos, " (", fmt, ")") ;
        }
        break ;
      case SVTYPE_BUNDLE :
        bitarray_or(sarray, sarray, barray + p->i * nbits, n) ;
        if (verbosity >= 4)
        {
          char fmt[UINT_FMT] ;
          fmt[uint_fmt(fmt, nlong + p->i)] = 0 ;
          strerr_warnt4x("atomic ", data.s + me->name, " depends on bundle ", data.s + p->pos) ;
        }
        break ;
      default :
        strerr_dief4x(1, "during dependency resolution for service ", data.s + me->name, ": undefined service name ", data.s + p->pos) ;
    }
  }
}

static inline void check_logger (longrun_t const *longruns, unsigned int i)
{
  unsigned int j ;
  register nameinfo_t const *p ;
  if (!longruns[i].logtype) return ;
  avltree_search(&names_map, data.s + longruns[i].logrelated, &j) ;
  p = genalloc_s(nameinfo_t, &nameinfo) + j ;
  switch (p->type)
  {
    case SVTYPE_LONGRUN :
    {
      unsigned int k ;
      register nameinfo_t const *q ;
      if (longruns[p->i].logtype != 3 - longruns[i].logtype) goto err ;
      avltree_search(&names_map, data.s + longruns[p->i].logrelated, &k) ;
      q = genalloc_s(nameinfo_t, &nameinfo) + k ;
      if (q->type != SVTYPE_LONGRUN) goto err ;
      if (q->i != i) goto err ;
      break ;
 err:
      strerr_dief7x(1, "longrun service ", data.s + longruns[i].common.name, " declares a ", longruns[i].logtype == 2 ? "producer" : "logger", " named ", data.s + p->pos, " that does not declare it back ") ;
    }
    case SVTYPE_ONESHOT :
    case SVTYPE_BUNDLE :
      strerr_dief8x(1, "longrun service ", data.s + longruns[i].common.name, " declares a ", longruns[i].logtype == 2 ? "producer" : "logger", " named ", data.s + p->pos, " of type ", p->type == SVTYPE_BUNDLE ? "bundle" : "oneshot") ;
    default :
      strerr_dief7x(1, "longrun service ", data.s + longruns[i].common.name, " declares a ", longruns[i].logtype == 2 ? "producer" : "logger", " named ", data.s + p->pos, " that is not defined") ;
  }
}

static inline unsigned int ugly_bitarray_vertical_countones (unsigned char const *sarray, unsigned int n, unsigned int i)
{
  unsigned int m = 0, j = 0, nbits = bitarray_div8(n) ;
  for (; j < n ; j++)
    if (bitarray_peek(sarray + j * nbits, i)) m++ ;
  return m ;
}

static inline unsigned int resolve_services (s6rc_db_t *db, before_t const *be, char const **srcdirs, unsigned char *sarray, unsigned char const *barray)
{
  oneshot_t const *oneshots = genalloc_s(oneshot_t const, &be->oneshots) ;
  longrun_t const *longruns = genalloc_s(longrun_t const, &be->longruns) ;
  uint32 const *indices = genalloc_s(uint32 const, &be->indices) ;
  unsigned int n = db->nshort + db->nlong ;
  unsigned int nbits = bitarray_div8(n) ;
  unsigned int total[2] = { 0, 0 } ;
  unsigned int i = 0 ;
  if (verbosity >= 2) strerr_warni1x("resolving service names") ;
  byte_zero(sarray, nbits * n) ;
  for (; i < db->nlong ; i++)
  {
    db->services[i].type = 1 ;
    db->services[i].name = longruns[i].common.kname ;
    db->services[i].flags = longruns[i].common.annotation_flags ;
    db->services[i].timeout[0] = longruns[i].common.timeout[0] ;
    db->services[i].timeout[1] = longruns[i].common.timeout[1] ;
    db->services[i].x.longrun.servicedir = longruns[i].servicedirname ;
    srcdirs[i] = longruns[i].srcdir ;
    resolve_deps(&longruns[i].common, db->nlong, n, nbits, indices, sarray + i * nbits, barray) ;
    check_logger(longruns, i) ;
  }
  for (i = 0 ; i < db->nshort ; i++)
  {
    db->services[db->nlong + i].type = 0 ;
    db->services[db->nlong + i].name = oneshots[i].common.kname ;
    db->services[db->nlong + i].flags = oneshots[i].common.annotation_flags ;
    db->services[db->nlong + i].timeout[0] = oneshots[i].common.timeout[0] ;
    db->services[db->nlong + i].timeout[1] = oneshots[i].common.timeout[1] ;
    db->services[db->nlong + i].x.oneshot.argc[0] = oneshots[i].argc[0] ;
    db->services[db->nlong + i].x.oneshot.argv[0] = oneshots[i].argvindex[0] ;
    db->services[db->nlong + i].x.oneshot.argc[1] = oneshots[i].argc[1] ;
    db->services[db->nlong + i].x.oneshot.argv[1] = oneshots[i].argvindex[1] ;
    resolve_deps(&oneshots[i].common, db->nlong, n, nbits, indices, sarray + (db->nlong + i) * nbits, barray) ;
  }

  for (i = 0 ; i < n ; i++)
  {
    db->services[i].deps[0] = total[0] ;
    db->services[i].ndeps[0] = ugly_bitarray_vertical_countones(sarray, n, i) ;
    total[0] += db->services[i].ndeps[0] ;
    db->services[i].deps[1] = total[1] ;
    db->services[i].ndeps[1] = bitarray_countones(sarray + i * nbits, n) ;
    total[1] += db->services[i].ndeps[1] ;
  }
  return total[1] ;
}

static inline void flatlist_services (s6rc_db_t *db, unsigned char const *sarray)
{
  unsigned int n = db->nshort + db->nlong ;
  unsigned int nbits = bitarray_div8(n) ;
  unsigned int i = 0 ;
  if (verbosity >= 3) strerr_warni1x("converting service dependency array") ;
  for (; i < n ; i++)
  {
    uint32 *mydeps = db->deps + db->services[i].deps[0] ;
    unsigned int j = 0, k = 0 ;
    for (; k < db->services[i].ndeps[0] ; j++)
      if (bitarray_peek(sarray + j * nbits, i)) mydeps[k++] = j ;
    mydeps = db->deps + db->ndeps + db->services[i].deps[1] ;
    j = 0 ; k = 0 ;
    for (; k < db->services[i].ndeps[1] ; j++)
      if (bitarray_peek(sarray + i * nbits, j)) mydeps[k++] = j ;
  }
  if (verbosity >= 3) strerr_warni1x("checking for dependency cycles") ;
  nbits = s6rc_db_check_depcycles(db, 1, &i) ;
  if (nbits < n)
    strerr_dief4x(1, "cyclic service definition: resolution of ", db->string + db->services[nbits].name, " encountered a cycle involving ", db->string + db->services[i].name) ;
}



 /* Write the compiled database */


static void cleanup (char const *compiled)
{
  int e = errno ;
  rm_rf(compiled) ;
  errno = e ;
}

static void auto_dir (char const *compiled, char const *dir)
{
  unsigned int clen = str_len(compiled) ;
  unsigned int dlen = str_len(dir) ;
  char fn[clen + dlen + 2] ;
  byte_copy(fn, clen, compiled) ;
  fn[clen] = dlen ? '/' : 0 ;
  byte_copy(fn + clen + 1, dlen + 1, dir) ;
  if (mkdir(fn, 0755) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "mkdir ", fn) ;
  }
}

static void auto_file (char const *compiled, char const *file, char const *s, unsigned int n)
{
  unsigned int clen = str_len(compiled) ;
  unsigned int flen = str_len(file) ;
  char fn[clen + flen + 2] ;
  byte_copy(fn, clen, compiled) ;
  fn[clen] = '/' ;
  byte_copy(fn + clen + 1, flen + 1, file) ;
  if (!openwritenclose_unsafe(fn, s, n))
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "write to ", fn) ;
  }
}

static void auto_rights (char const *compiled, char const *file, mode_t mode)
{
  unsigned int clen = str_len(compiled) ;
  unsigned int flen = str_len(file) ;
  char fn[clen + flen + 2] ;
  byte_copy(fn, clen, compiled) ;
  fn[clen] = '/' ;
  byte_copy(fn + clen + 1, flen + 1, file) ;
  if (chmod(fn, mode) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "chmod ", fn) ;
  }
}

static inline void init_compiled (char const *compiled)
{
  if (mkdir(compiled, 0755) < 0)
    strerr_diefu2sys(111, "mkdir ", compiled) ;
  auto_dir(compiled, "servicedirs") ;
}

static inline void write_sizes (char const *compiled, s6rc_db_t const *db)
{
  char pack[20] ;
  if (verbosity >= 3) strerr_warni3x("writing ", compiled, "/n") ;
  uint32_pack_big(pack, (uint32)db->nshort) ;
  uint32_pack_big(pack + 4, (uint32)db->nlong) ;
  uint32_pack_big(pack + 8, (uint32)db->stringlen) ;
  uint32_pack_big(pack + 12, (uint32)db->nargvs) ;
  uint32_pack_big(pack + 16, (uint32)db->ndeps) ;
  auto_file(compiled, "n", pack, 20) ;
}

static inline void write_specials (char const *compiled)
{
  auto_dir(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER) ;
  auto_file(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/notification-fd", "3\n", 2) ;
  auto_dir(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/data") ;
  auto_dir(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules") ;
  auto_dir(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules/uid") ;
  auto_dir(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules/uid/default") ;
  auto_file(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules/uid/default/deny", "", 0) ;
  auto_dir(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules/uid/0") ;
  auto_file(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules/uid/0/allow", "", 0) ;
  auto_file(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/run", S6RC_ONESHOT_RUNNER_RUNSCRIPT, sizeof(S6RC_ONESHOT_RUNNER_RUNSCRIPT) - 1) ;
  auto_rights(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/run", 0755) ;
}

static inline void write_resolve (char const *compiled, s6rc_db_t const *db, bundle_t const *bundles, unsigned int nbundles, uint32 const *bdeps)
{
  unsigned int clen = str_len(compiled) ;
  int fd ;
  struct cdb_make c = CDB_MAKE_ZERO ;
  unsigned int i = db->nshort + db->nlong ;
  char fn[clen + 13] ;
  if (verbosity >= 3) strerr_warni3x("writing ", compiled, "/resolve.cdb") ;
  byte_copy(fn, clen, compiled) ;
  byte_copy(fn + clen, 13, "/resolve.cdb") ;
  fd = open_trunc(fn) ;
  if (fd < 0 || ndelay_off(fd) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "open_trunc ", fn) ;
  }
  if (cdb_make_start(&c, fd) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "cdb_make_start on ", fn) ;
  }

 /* atomic services */
  while (i--)
  {
    char pack[4] ;
    uint32_pack_big(pack, (uint32)i) ;
    if (cdb_make_add(&c, db->string + db->services[i].name, str_len(db->string + db->services[i].name), pack, 4) < 0)
    {
      cleanup(compiled) ;
      strerr_diefu1sys(111, "cdb_make_add") ;
    }
  }

 /* bundles */
  i = nbundles ;
  while (i--)
  {
    unsigned int j = 0 ;
    char pack[(bundles[i].n << 2) + 1] ; /* +1 because braindead C standard */
    for (; j < bundles[i].n ; j++)
      uint32_pack_big(pack + (j << 2), bdeps[bundles[i].listindex + j]) ;
    if (cdb_make_add(&c, data.s + bundles[i].name, str_len(data.s + bundles[i].name), pack, bundles[i].n << 2) < 0)
    {
      cleanup(compiled) ;
      strerr_diefu1sys(111, "cdb_make_add") ;
    }
  }

  if (cdb_make_finish(&c) < 0 || fsync(fd) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "write to ", fn) ;
  }
  close(fd) ;
}

static int filecopy (char const *src, char const *dst, mode_t mode)
{
  int d ;
  int s = open_readb(src) ;
  if (s < 0) return 0 ;
  d = open3(dst, O_WRONLY | O_CREAT | O_TRUNC, mode) ;
  if (d < 0)
  {
    close(s) ;
    return 0 ;
  }
  if (fd_cat(s, d) < 0) goto err ;
  close(s) ;
  close(d) ;
  return 1 ;

err:
  {
    register int e = errno ;
    close(s) ;
    close(d) ;
    errno = e ;
  }
  return 0 ;
}

static void dircopy (char const *compiled, char const *srcfn, char const *dstfn)
{
  struct stat st ;
  if (stat(srcfn, &st) < 0)
  {
    if (errno == ENOENT) return ;
    cleanup(compiled) ;
    strerr_diefu2sys(111, "stat ", srcfn) ;
  }
  if (!S_ISDIR(st.st_mode))
  {
    cleanup(compiled) ;
    strerr_dief2x(1, srcfn, " is not a directory") ;
  }
  if (!hiercopy(srcfn, dstfn))
  {
    cleanup(compiled) ;
    strerr_diefu4sys(111, "recursively copy ", srcfn, " to ", dstfn) ;
  }
}

static void write_servicedir (char const *compiled, char const *srcdir, char const *src, char const *dst)
{
  unsigned int clen = str_len(compiled) ;
  unsigned int srcdirlen = str_len(srcdir) ;
  unsigned int srclen = str_len(src) ;
  unsigned int dstlen = str_len(dst) ;
  struct stat st ;
  char dstfn[clen + 30 + dstlen] ;
  char srcfn[srcdirlen + srclen + 18] ;
  byte_copy(dstfn, clen, compiled) ;
  byte_copy(dstfn + clen, 13, "/servicedirs/") ;
  byte_copy(dstfn + clen + 13, dstlen + 1, dst) ;
  if (mkdir(dstfn, 0755) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "mkdir ", dstfn) ;
  }
  byte_copy(dstfn + clen + 13 + dstlen, 5, "/run") ;
  byte_copy(srcfn, srcdirlen, srcdir) ;
  srcfn[srcdirlen] = '/' ;
  byte_copy(srcfn + srcdirlen + 1, srclen, src) ;
  byte_copy(srcfn + srcdirlen + srclen + 1, 5, "/run") ;
  if (!filecopy(srcfn, dstfn, 0755))
  {
    cleanup(compiled) ;
    strerr_diefu4sys(111, "copy ", srcfn, " to ", dstfn) ;
  }
  byte_copy(dstfn + clen + 14 + dstlen, 7, "finish") ;
  byte_copy(srcfn + srcdirlen + srclen + 2, 7, "finish") ;
  filecopy(srcfn, dstfn, 0755) ;
  byte_copy(dstfn + clen + 14 + dstlen, 16, "notification-fd") ;
  byte_copy(srcfn + srcdirlen + srclen + 2, 16, "notification-fd") ;
  filecopy(srcfn, dstfn, 0755) ;

  byte_copy(srcfn + srcdirlen + srclen + 2, 9, "nosetsid") ;
  if (stat(srcfn, &st) < 0)
  {
    if (errno != ENOENT)
    {
      cleanup(compiled) ;
      strerr_diefu2sys(111, "stat ", srcfn) ;
    }
  }
  else
  {
    int fd ;
    byte_copy(dstfn + clen + 14 + dstlen, 9, "nosetsid") ;
    fd = open_trunc(dstfn) ;
    if (fd < 0)
    {
      cleanup(compiled) ;
      strerr_diefu2sys(111, "touch ", dstfn) ;
    }
    close(fd) ;
  } 

  byte_copy(dstfn + clen + 14 + dstlen, 5, "data") ;
  byte_copy(srcfn + srcdirlen + srclen + 2, 5, "data") ;
  dircopy(compiled, srcfn, dstfn) ;
  byte_copy(dstfn + clen + 14 + dstlen, 4, "env") ;
  byte_copy(srcfn + srcdirlen + srclen + 2, 4, "env") ;
  dircopy(compiled, srcfn, dstfn) ;
}

static inline void write_servicedirs (char const *compiled, s6rc_db_t const *db, char const *const *srcdirs, unsigned int maxnamelen)
{
  unsigned int clen = str_len(compiled) ;
  unsigned int i = 1 ;
  char fn[clen + 23 + maxnamelen] ;
  char islogger[db->nlong] ;
  if (verbosity >= 3) strerr_warni3x("writing ", compiled, "/servicedirs") ;
  byte_copy(fn, clen, compiled) ;
  byte_copy(fn + clen, 12, "/servicedirs") ;
  fn[clen+12] = '/' ;
  byte_zero(islogger, db->nlong) ;
  for (; i < db->nlong ; i++)
  {
    char const *servicedirname = db->string + db->services[i].x.longrun.servicedir ;
    islogger[i] = servicedirname[str_chr(servicedirname, '/')] ;
    if (!islogger[i])
      write_servicedir(compiled, srcdirs[i], db->string + db->services[i].name, servicedirname) ;
  }
  for (i = 1 ; i < db->nlong ; i++)
  {
    if (islogger[i])
      write_servicedir(compiled, srcdirs[i], db->string + db->services[i].name, db->string + db->services[i].x.longrun.servicedir) ;
  }
}

static inline int write_service (buffer *b, s6rc_service_t const *sv)
{
  char pack[50] ;
  unsigned int m ;
  uint32_pack_big(pack, sv->name) ;
  uint32_pack_big(pack + 4, sv->flags) ;
  uint32_pack_big(pack + 8, sv->timeout[0]) ;
  uint32_pack_big(pack + 12, sv->timeout[1]) ;
  uint32_pack_big(pack + 16, sv->ndeps[0]) ;
  uint32_pack_big(pack + 20, sv->ndeps[1]) ;
  uint32_pack_big(pack + 24, sv->deps[0]) ;
  uint32_pack_big(pack + 28, sv->deps[1]) ;
  pack[32] = sv->type ;
  if (sv->type)
  {
    uint32_pack_big(pack + 33, sv->x.longrun.servicedir) ;
    m = 37 ;
  }
  else
  {
    uint32_pack_big(pack + 33, sv->x.oneshot.argc[0]) ;
    uint32_pack_big(pack + 37, sv->x.oneshot.argv[0]) ;
    uint32_pack_big(pack + 41, sv->x.oneshot.argc[1]) ;
    uint32_pack_big(pack + 45, sv->x.oneshot.argv[1]) ;
    m = 49 ;
  }
  pack[m++] = '\376' ;
  return (buffer_put(b, pack, m) == m) ;
}

static inline void write_db (char const *compiled, s6rc_db_t const *db)
{
  unsigned int clen = str_len(compiled) ;
  buffer b ;
  int fd ;
  char buf[4096] ;
  char dbfn[clen + 4] ;
  byte_copy(dbfn, clen, compiled) ;
  byte_copy(dbfn + clen, 4, "/db") ;
  if (verbosity >= 3) strerr_warni2x("writing ", dbfn) ;
  fd = open_trunc(dbfn) ;
  if (fd < 0)
  {
    cleanup(compiled) ;
    strerr_diefu3sys(111, "open ", dbfn, " for writing") ;
  }
  if (ndelay_off(fd) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "ndelay_off ", dbfn) ;
  }
  buffer_init(&b, &fd_writesv, fd, buf, 4096) ;

  if (buffer_put(&b, S6RC_DB_BANNER_START, S6RC_DB_BANNER_START_LEN) < 0)
    goto err ;

  if (buffer_put(&b, db->string, db->stringlen) < 0) goto err ;

  {
    unsigned int i = db->ndeps << 1 ;
    uint32 const *deps = db->deps ;
    while (i--)
    {
      char pack[4] ;
      uint32_pack_big(pack, *deps++) ;
      if (buffer_put(&b, pack, 4) < 0) goto err ;
    }
  }

  {
    unsigned int i = db->nshort + db->nlong ;
    s6rc_service_t const *sv = db->services ;
    while (i--) if (!write_service(&b, sv++)) goto err ;
  }

  if (buffer_putflush(&b, S6RC_DB_BANNER_END, S6RC_DB_BANNER_END_LEN) < 0)
    goto err ;

  if (fsync(fd) < 0) goto err ;
  close(fd) ;
  return ;
 err:
  cleanup(compiled) ;
  strerr_diefu2sys(111, "write to ", dbfn) ;
}

static inline void write_compiled (char const *compiled, s6rc_db_t const *db, char const *const *srcdirs, bundle_t const *bundles, unsigned int nbundles, uint32 const *bdeps, unsigned int maxnamelen)
{
  if (verbosity >= 2) strerr_warni2x("writing compiled information to ", compiled) ;
  init_compiled(compiled) ;
  write_sizes(compiled, db) ;
  write_resolve(compiled, db, bundles, nbundles, bdeps) ;
  write_db(compiled, db) ;
  write_specials(compiled) ;
  write_servicedirs(compiled, db, srcdirs, maxnamelen) ;
}

int main (int argc, char const *const *argv)
{
  before_t before = BEFORE_ZERO ;
  char const *compiled ;
  PROG = "s6-rc-compile" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "v:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  if (argc < 2) dieusage() ;
  compiled = *argv++ ;
  add_specials(&before) ;
  for (; *argv ; argv++) add_sources(&before, *argv) ;

  {
    unsigned int n = genalloc_len(oneshot_t, &before.oneshots) + genalloc_len(longrun_t, &before.longruns) ;
    unsigned int nbits = bitarray_div8(n) ;
    unsigned int nbundles = genalloc_len(bundle_t, &before.bundles) ;
    unsigned int maxnamelen = before.maxnamelen ;
    s6rc_service_t servicesblob[n] ;
    s6rc_db_t db =
    {
      .services = servicesblob,
      .nshort = genalloc_len(oneshot_t, &before.oneshots),
      .nlong = genalloc_len(longrun_t, &before.longruns),
      .stringlen = keep.len,
      .nargvs = before.nargvs,
      .string = keep.s
    } ;
    char const *srcdirs[db.nlong] ;
    unsigned char sarray[nbits * n] ;
    bundle_t bundles[nbundles] ;
    unsigned char barray[nbits * nbundles] ;
    unsigned int nbdeps = resolve_bundles(genalloc_s(bundle_t, &before.bundles), bundles, db.nshort, db.nlong, nbundles, genalloc_s(uint32, &before.indices), barray) ;
    uint32 bdeps[nbdeps] ;

    genalloc_free(bundle_t, &before.bundles) ;
    flatlist_bundles(bundles, nbundles, nbits, barray, bdeps) ;
    db.ndeps = resolve_services(&db, &before, srcdirs, sarray, barray) ;
    genalloc_free(uint32, &before.indices) ;
    genalloc_free(oneshot_t, &before.oneshots) ;
    genalloc_free(longrun_t, &before.longruns) ;
    genalloc_free(nameinfo_t, &nameinfo) ;
    avltree_free(&names_map) ;
    {
      uint32 deps[db.ndeps << 1] ;
      db.deps = deps ;
      flatlist_services(&db, sarray) ;
      write_compiled(compiled, &db, srcdirs, bundles, nbundles, bdeps, maxnamelen) ;
    }
  }

  return 0 ;
}
