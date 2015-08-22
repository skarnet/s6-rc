/* ISC license. */

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <skalibs/uint32.h>
#include <skalibs/uint64.h>
#include <skalibs/uint.h>
#include <skalibs/gidstuff.h>
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
#include <s6-rc/config.h>
#include <s6-rc/s6rc.h>

#define USAGE "s6-rc-compile [ -v verbosity ] [ -u okuid,okuid... ] [ -g okgid,okgid... ] [ -h fdholder_user ] destdir sources..."
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_dief1x(111, "out of memory") ;

#define S6RC_INTERNALS "s6-rc-compile internals"

#define S6RC_ONESHOT_RUNNER_RUNSCRIPT \
"#!" EXECLINE_SHEBANGPREFIX "execlineb -P\n" \
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
  unsigned int argvindex[2] ; /* pos in keep */
  unsigned int argc[2] ;
} ;

typedef struct longrun_s longrun_t, *longrun_t_ref ;
struct longrun_s
{
  common_t common ;
  char const *srcdir ;
  unsigned int pipeline[2] ; /* pos in data */
  unsigned int pipelinename ; /* pos in data */
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
  genalloc indices ; /* unsigned int */
  genalloc oneshots ; /* oneshot_t */
  genalloc longruns ; /* longrun_t */
  genalloc bundles ; /* bundle_t */
  unsigned int nargvs ;
  uint32 specialdeps[2] ;
} ;

#define BEFORE_ZERO { .indices = GENALLOC_ZERO, .oneshots = GENALLOC_ZERO, .longruns = GENALLOC_ZERO, .bundles = GENALLOC_ZERO, .nargvs = 0, .specialdeps = { 0, 0 } } ;



 /* Read all the sources, populate the name map */


static char const *typestr (servicetype_t type)
{
  return (type == SVTYPE_ONESHOT) ? "oneshot" :
         (type == SVTYPE_LONGRUN) ? "longrun" :
         (type == SVTYPE_BUNDLE) ? "bundle" :
         "unknown" ;
}

static int add_name_nocheck (before_t *be, char const *srcdir, char const *name, servicetype_t type, unsigned int *pos, unsigned int *kpos)
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
    if (type == SVTYPE_ONESHOT || type == SVTYPE_LONGRUN)
      if (!stralloc_catb(&keep, name, namelen + 1)) dienomem() ;
    if (!stralloc_catb(&data, name, namelen + 1)) dienomem() ;
    if (!genalloc_append(nameinfo_t, &nameinfo, &info)) dienomem() ;
    if (!avltree_insert(&names_map, i)) dienomem() ;
    *pos = info.pos ;
    *kpos = info.kpos ;
    return 0 ;
  }
}

static void check_identifier (char const *srcdir, char const *s)
{
  if (!byte_diff(s, 5, "s6rc-") && !byte_diff(s, 6, "s6-rc-"))
    strerr_dief5x(1, "in ", srcdir, ": identifier ", s, " starts with reserved prefix") ;
}

static int add_name (before_t *be, char const *srcdir, char const *name, servicetype_t type, unsigned int *pos, unsigned int *kpos)
{
  check_identifier(srcdir, name) ;
  return add_name_nocheck(be, srcdir, name, type, pos, kpos) ;
}

static unsigned int add_internal_longrun (before_t *be, char const *name)
{
  unsigned int pos ;
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
    .pipeline = { 0, 0 }
  } ;
  add_name_nocheck(be, S6RC_INTERNALS, name, SVTYPE_LONGRUN, &pos, &service.common.kname) ;
  if (!genalloc_append(longrun_t, &be->longruns, &service)) dienomem() ;
  return pos ;
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

static void read_script (before_t *be, int dirfd, char const *srcdir, char const *name, char const *script, unsigned int *argvindex, unsigned int *argc, int mandatory)
{
  int r = 0 ;
  int fd = open_readatb(dirfd, script) ;
  *argvindex = keep.len ;
  if (fd < 0)
  {
    if (errno != ENOENT || mandatory)
      strerr_diefu6sys(111, "open ", srcdir, "/", name, "/", script) ;
  }
  else
  {
    char buf[4096] ;
    buffer b = BUFFER_INIT(&fd_readsv, fd, buf, 4096) ;
    r = el_parse_from_buffer(&keep, &b) ;
    switch (r)
    {
      case -3 : strerr_dief7x(1, "syntax error in ", srcdir, "/", name, "/", script, ": missing }");
      case -2 : strerr_dief6x(1, "syntax error in ", srcdir, "/", name, "/", script) ;
      case -1 : strerr_diefu6sys(111, "parse ", srcdir, "/", name, "/", script) ;
    }
    close(fd) ;
  }
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
  unsigned int dummy ;
  common->annotation_flags = 0 ;
  add_name(be, srcdir, name, svtype, &dummy, &common->kname) ;
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
  oneshot_t service ;
  if (verbosity >= 3) strerr_warni3x(name, " has type ", "oneshot") ;
  add_common(be, dirfd, srcdir, name, &service.common, SVTYPE_ONESHOT) ;
  read_script(be, dirfd, srcdir, name, "up", &service.argvindex[1], &service.argc[1], 1) ;
  read_script(be, dirfd, srcdir, name, "down", &service.argvindex[0], &service.argc[0], 0) ;
  if (!genalloc_append(unsigned int, &be->indices, &be->specialdeps[0])) dienomem() ;
  service.common.ndeps++ ;
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
  longrun_t service = { .srcdir = srcdir, .pipeline = { 0, 0 }, .pipelinename = 0 } ;
  unsigned int relatedindex, n ;
  int fd ;
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
  fd = 0 ;
  if (add_namelist(be, dirfd, srcdir, name, "producer-for", &relatedindex, &n))
  {
    if (n != 1)
      strerr_dief5x(1, srcdir, "/", name, "/producer-for", " should only contain one service name") ;
    service.pipeline[1] = genalloc_s(unsigned int, &be->indices)[relatedindex] ;
    if (!genalloc_append(unsigned int, &be->indices, &be->specialdeps[1])) dienomem() ;
    service.common.ndeps += 2 ;
    if (verbosity >= 3)
      strerr_warni3x(name, " is a producer for ", data.s + service.pipeline[1]) ;
    fd = 1 ;
  }
  if (add_namelist(be, dirfd, srcdir, name, "consumer-for", &relatedindex, &n))
  {
    if (n != 1)
      strerr_dief5x(1, srcdir, "/", name, "/consumer-for", " should only contain one service name") ;
    service.pipeline[0] = genalloc_s(unsigned int, &be->indices)[relatedindex] ;
    genalloc_setlen(unsigned int, &be->indices, relatedindex) ;
    if (!fd)
    {
      genalloc_append(unsigned int, &be->indices, &be->specialdeps[1]) ;
      service.common.ndeps++ ;
    }
    else fd = 0 ;
    if (verbosity >= 3)
      strerr_warni3x(name, " is a consumer for ", data.s + service.pipeline[0]) ;
  }
  if (fd && add_namelist(be, dirfd, srcdir, name, "pipeline-name", &relatedindex, &n))
  {
    if (n != 1)
      strerr_dief5x(1, srcdir, "/", name, "/pipeline-name", " should only contain one name") ;
    service.pipelinename = genalloc_s(unsigned int, &be->indices)[relatedindex] ;
    genalloc_setlen(unsigned int, &be->indices, relatedindex) ;
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
  unsigned int start = satmp.len ;
  unsigned int cur ;
  DIR *dir = opendir(srcdir) ;
  if (!dir) strerr_diefu2sys(111, "opendir ", srcdir) ;
  if (!stralloc_cats(&satmp, srcdir) || !stralloc_catb(&satmp, "/", 1)) dienomem() ;
  cur = satmp.len ;
  for (;;)
  {
    struct stat st ;
    int fd ;
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    if (d->d_name[str_chr(d->d_name, '\n')])
      strerr_dief3x(1, "subdirectory of ", srcdir, " contains a newline character") ;
    check_identifier(srcdir, d->d_name) ;
    satmp.len = cur ;
    if (!stralloc_catb(&satmp, d->d_name, str_len(d->d_name) + 1)) dienomem() ;
    if (stat(satmp.s + start, &st) < 0)
      strerr_diefu2sys(111, "stat ", satmp.s + start) ;
    if (!S_ISDIR(st.st_mode)) continue ;
    fd = open_readb(satmp.s + start) ;
    if (fd < 0) strerr_diefu2sys(111, "open ", satmp.s + start) ;
    add_source(be, fd, srcdir, d->d_name) ;
    close(fd) ;
  }
  if (errno) strerr_diefu2sys(111, "readdir ", srcdir) ;
  dir_close(dir) ;
  satmp.len = start ;
}

static inline void add_pipeline_bundles (before_t *be)
{
  longrun_t const *longruns = genalloc_s(longrun_t, &be->longruns) ;
  unsigned int n = genalloc_len(longrun_t, &be->longruns) ;
  unsigned int i = n ;
  while (i--) if (longruns[i].pipelinename)
  {
    bundle_t bundle = { .listindex = genalloc_len(unsigned int, &be->indices), .n = 1 } ;
    unsigned int id ;
    nameinfo_t const *info ;
    unsigned int j = i ;
    if (verbosity >= 3) strerr_warni2x("creating bundle for pipeline ", data.s + longruns[i].pipelinename) ;
    add_name(be, S6RC_INTERNALS, data.s + longruns[i].pipelinename, SVTYPE_BUNDLE, &bundle.name, &id) ;
    avltree_search(&names_map, keep.s + longruns[i].common.kname, &id) ;
    info = genalloc_s(nameinfo_t, &nameinfo) + id ;
    if (!genalloc_append(unsigned int, &be->indices, &info->pos)) dienomem() ;

    while (longruns[j].pipeline[1])
    {
      if (bundle.n >= n)
        strerr_dief4x(1, "pipeline ", data.s + longruns[i].pipelinename, " is too long: possible loop involving ", keep.s + longruns[j].common.kname) ;
      avltree_search(&names_map, data.s + longruns[j].pipeline[1], &id) ;
      info = genalloc_s(nameinfo_t, &nameinfo) + id ;
      if (info->type != SVTYPE_LONGRUN)
        strerr_dief5x(1, "longrun service ", keep.s + longruns[j].common.kname, " declares a consumer ", data.s + longruns[j].pipeline[1], " that is not a longrun service") ;
      if (!genalloc_append(unsigned int, &be->indices, &info->pos)) dienomem() ;
      j = info->i ;
      bundle.n++ ;
    }
    if (!genalloc_append(bundle_t, &be->bundles, &bundle)) dienomem() ;
  }
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
  unsigned int const *indices ;
  unsigned char *barray ;
  unsigned char *mark ;
  unsigned int source ;
} ;

static void resolve_bundle_rec (bundle_recinfo_t *recinfo, unsigned int i)
{
  if (!(recinfo->mark[i] & 2))
  {
    bundle_t const *me = recinfo->oldb + i ;
    unsigned int const *listindex = recinfo->indices + me->listindex ;
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

static inline unsigned int resolve_bundles (bundle_t const *oldb, bundle_t *newb, unsigned int nshort, unsigned int nlong, unsigned int nbundles, unsigned int const *indices, unsigned char *barray)
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

static void resolve_deps (common_t const *me, unsigned int nlong, unsigned int n, unsigned int nbits, unsigned int const *indices, unsigned char *sarray, unsigned char const *barray)
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
          strerr_warnt6x(keep.s + me->kname, " depends on oneshot ", data.s + p->pos, " (", fmt, ")") ;
        }
        break ;
      case SVTYPE_LONGRUN :
        bitarray_set(sarray, p->i) ;
        if (verbosity >= 4)
        {
          char fmt[UINT_FMT] ;
          fmt[uint_fmt(fmt, p->i)] = 0 ;
          strerr_warnt6x(keep.s + me->kname, " depends on longrun ", data.s + p->pos, " (", fmt, ")") ;
        }
        break ;
      case SVTYPE_BUNDLE :
        bitarray_or(sarray, sarray, barray + p->i * nbits, n) ;
        if (verbosity >= 4)
        {
          char fmt[UINT_FMT] ;
          fmt[uint_fmt(fmt, nlong + p->i)] = 0 ;
          strerr_warnt3x(keep.s + me->kname, " depends on bundle ", data.s + p->pos) ;
        }
        break ;
      default :
        strerr_dief4x(1, "during dependency resolution for service ", keep.s + me->kname, ": undefined service name ", data.s + p->pos) ;
    }
  }
}

static uint32 resolve_prodcons (longrun_t const *longruns, unsigned int i, int h, uint32 nlong)
{
  unsigned int j ;
  register nameinfo_t const *p ;
  if (!longruns[i].pipeline[h]) return nlong ;
  avltree_search(&names_map, data.s + longruns[i].pipeline[h], &j) ;
  p = genalloc_s(nameinfo_t, &nameinfo) + j ;
  switch (p->type)
  {
    case SVTYPE_LONGRUN :
    {
      unsigned int k ;
      register nameinfo_t const *q ;
      avltree_search(&names_map, data.s + longruns[p->i].pipeline[!h], &k) ;
      q = genalloc_s(nameinfo_t, &nameinfo) + k ;
      if (q->type != SVTYPE_LONGRUN) goto err ;
      if (q->i != i) goto err ;
      break ;
     err:
      strerr_dief7x(1, "longrun service ", keep.s + longruns[i].common.kname, " declares being a ", h ? "producer" : "consumer", " for a service named ", data.s + p->pos, " that does not declare it back ") ;
    }
    case SVTYPE_ONESHOT :
    case SVTYPE_BUNDLE :
      strerr_dief8x(1, "longrun service ", keep.s + longruns[i].common.kname, " declares being a ", h ? "producer" : "consumer", " for a service named ", data.s + p->pos, " of type ", p->type == SVTYPE_BUNDLE ? "bundle" : "oneshot") ;
    default :
      strerr_dief6x(1, "longrun service ", keep.s + longruns[i].common.kname, " declares being a ", h ? "producer" : "consumer", " for an undefined service: ", data.s + p->pos) ;
  }
  return p->i ;
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
  unsigned int const *indices = genalloc_s(unsigned int const, &be->indices) ;
  unsigned int n = db->nshort + db->nlong ;
  unsigned int nbits = bitarray_div8(n) ;
  unsigned int total[2] = { 0, 0 } ;
  unsigned int i = 0 ;
  if (verbosity >= 2) strerr_warni1x("resolving service names") ;
  byte_zero(sarray, nbits * n) ;
  for (; i < db->nlong ; i++)
  {
    srcdirs[i] = longruns[i].srcdir ;
    db->services[i].name = longruns[i].common.kname ;
    db->services[i].flags = longruns[i].common.annotation_flags ;
    db->services[i].timeout[0] = longruns[i].common.timeout[0] ;
    db->services[i].timeout[1] = longruns[i].common.timeout[1] ;
    db->services[i].x.longrun.pipeline[0] = resolve_prodcons(longruns, i, 0, db->nlong) ;
    db->services[i].x.longrun.pipeline[1] = resolve_prodcons(longruns, i, 1, db->nlong) ;
    resolve_deps(&longruns[i].common, db->nlong, n, nbits, indices, sarray + i * nbits, barray) ;
  }
  for (i = 0 ; i < db->nshort ; i++)
  {
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
  diuint32 problem ;
  unsigned int i = 0 ;
  register int r ;
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

  if (verbosity >= 3) strerr_warni1x("checking database correctness") ;
  if (s6rc_db_check_depcycles(db, 1, &problem))
    strerr_dief5x(1, "cyclic service dependency", " reached from ", db->string + db->services[problem.left].name, " and involving ", db->string + db->services[problem.right].name) ;
  r = s6rc_db_check_pipelines(db, &problem) ;
  if (r)
  {
    if (r == 1)
      strerr_dief5x(1, "cyclic longrun pipeline", " reached from ", db->string + db->services[problem.left].name, " and involving ", db->string + db->services[problem.right].name) ;
    else
      strerr_dief5x(1, "longrun pipeline collision", " reached from ", db->string + db->services[problem.left].name, " and involving ", db->string + db->services[problem.right].name) ;
  }
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

static void auto_symlink (char const *compiled, char const *name, char const *target)
{
  unsigned int clen = str_len(compiled) ;
  unsigned int flen = str_len(name) ;
  char fn[clen + flen + 2] ;
  byte_copy(fn, clen, compiled) ;
  fn[clen] = '/' ;
  byte_copy(fn + clen + 1, flen + 1, name) ;
  if (symlink(target, fn) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu4sys(111, "symlink ", target, " to ", fn) ;
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

static void make_skel (char const *compiled, char const *name, uint64 const *uids, unsigned int uidn, gid_t const *gids, unsigned int gidn, unsigned int notif)
{
  unsigned int namelen = str_len(name) ;
  char fmt[UINT_FMT] ;
  unsigned int i = uint_fmt(fmt, notif) ;
  fmt[i++] = '\n' ;
  char fn[namelen + 29] ;
  byte_copy(fn, 12, "servicedirs/") ;
  byte_copy(fn + 12, namelen + 1, name) ;
  auto_dir(compiled, fn) ;
  byte_copy(fn + 12 + namelen, 17, "/notification-fd") ;
  auto_file(compiled, fn, fmt, i) ;
  byte_copy(fn + 13 + namelen, 5, "data") ;
  auto_dir(compiled, fn) ;
  byte_copy(fn + 17 + namelen, 7, "/rules") ;
  auto_dir(compiled, fn) ;
  if (gidn)
  {
    byte_copy(fn + 23 + namelen, 5, "/gid") ;
    auto_dir(compiled, fn) ;
  }
  byte_copy(fn + 23 + namelen, 5, "/uid") ;
  auto_dir(compiled, fn) ;
}

static inline void write_oneshot_runner (char const *compiled, uint64 const *uids, unsigned int uidn, gid_t const *gids, unsigned int gidn)
{
  unsigned int i ;
  char fn[34 + sizeof(S6RC_ONESHOT_RUNNER)] = "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules/gid/" ;
  make_skel(compiled, S6RC_ONESHOT_RUNNER, uids, uidn, gids, gidn, 3) ;
  if (gidn)
  {
    i = gidn ;
    while (i--)
    {
      unsigned int len = gid_fmt(fn + 28 + S6RC_ONESHOT_RUNNER_LEN, gids[i]) ;
      fn[28 + S6RC_ONESHOT_RUNNER_LEN + len] = 0 ;
      auto_dir(compiled, fn) ;
      byte_copy(fn + 28 + S6RC_ONESHOT_RUNNER_LEN + len, 7, "/allow") ;
      auto_file(compiled, fn, "", 0) ;
    }
  }
  fn[24 + S6RC_ONESHOT_RUNNER_LEN] = 'u' ;
  i = uidn ;
  while (i--)
  {
    unsigned int len = uint64_fmt(fn + 28 + S6RC_ONESHOT_RUNNER_LEN, uids[i]) ;
    fn[28 + S6RC_ONESHOT_RUNNER_LEN + len] = 0 ;
    auto_dir(compiled, fn) ;
    byte_copy(fn + 28 + S6RC_ONESHOT_RUNNER_LEN + len, 7, "/allow") ;
    auto_file(compiled, fn, "", 0) ;
  }
  auto_file(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/run", S6RC_ONESHOT_RUNNER_RUNSCRIPT, sizeof(S6RC_ONESHOT_RUNNER_RUNSCRIPT) - 1) ;
  auto_rights(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/run", 0755) ;
}

static inline int write_pipelines (stralloc *sa, s6rc_db_t const *db)
{
  uint32 i = db->nlong ;
  unsigned char black[bitarray_div8(db->nlong)] ;
  byte_zero(black, bitarray_div8(db->nlong)) ;
  while (i--) if (!bitarray_peek(black, i))
  {
    uint32 j = i ;
    for (;;)
    {
      register uint32 k = db->services[j].x.longrun.pipeline[0] ;
      if (k >= db->nlong) break ;
      j = k ;
    }
    for (;;)
    {
      register uint32 k = db->services[j].x.longrun.pipeline[1] ;
      bitarray_set(black, j) ;
      if (k >= db->nlong) break ;
      if (!string_quote(sa, db->string + db->services[k].name, str_len(db->string + db->services[k].name))
       || !stralloc_catb(sa, " ", 1)) return 0 ;
      j = k ;
    }
  }
  return 1 ;
}

static inline void write_fdholder (char const *compiled, s6rc_db_t const *db, uint64 const *uids, unsigned int uidn, gid_t const *gids, unsigned int gidn, char const *fdhuser)
{
  unsigned int base = satmp.len ;
  make_skel(compiled, S6RC_FDHOLDER, uids, uidn, gids, gidn, 1) ;
  {
    char fn[62 + S6RC_FDHOLDER_LEN + UINT64_FMT] = "servicedirs/" S6RC_FDHOLDER "/data/rules/uid/" ;
    char fmt[7 + UINT64_FMT] = "../uid/" ;
    unsigned int i = uint64_fmt(fmt + 7, uids[0]) ;
    fmt[7 + i] = 0 ;
    byte_copy(fn + 28 + S6RC_FDHOLDER_LEN, i + 1, fmt + 7) ;
    auto_dir(compiled, fn) ;
    byte_copy(fn + 28 + S6RC_FDHOLDER_LEN + i, 7, "/allow") ;
    auto_file(compiled, fn, "", 0) ;
    byte_copy(fn + 29 + S6RC_FDHOLDER_LEN + i, 4, "env") ;
    auto_dir(compiled, fn) ;
    byte_copy(fn + 32 + S6RC_FDHOLDER_LEN + i, 18, "/S6_FDHOLDER_LIST") ;
    auto_file(compiled, fn, "\n", 1) ;
    byte_copy(fn + 45 + S6RC_FDHOLDER_LEN + i, 12, "STORE_REGEX") ;
    auto_file(compiled, fn, "^pipe:s6rc-\n", 12) ;
    byte_copy(fn + 45 + S6RC_FDHOLDER_LEN + i, 15, "RETRIEVE_REGEX") ;
    auto_symlink(compiled, fn, "S6_FDHOLDER_STORE_REGEX") ;
    byte_copy(fn + 45 + S6RC_FDHOLDER_LEN + i, 8, "SETDUMP") ;
    auto_file(compiled, fn, "\n", 1) ;
    fn[45 + S6RC_FDHOLDER_LEN + i] = 'G' ;
    auto_file(compiled, fn, "\n", 1) ;
    
    for (i = 1 ; i < uidn ; i++)
    {
      unsigned int len = uint64_fmt(fn + 28 + S6RC_FDHOLDER_LEN, uids[i]) ;
      fn[28 + S6RC_FDHOLDER_LEN + len] = 0 ;
      auto_symlink(compiled, fn, fmt + 7) ;
    }
    fn[24 + S6RC_FDHOLDER_LEN] = 'g' ;
    i = gidn ;
    while (i--)
    {
      unsigned int len = gid_fmt(fn + 28 + S6RC_FDHOLDER_LEN, gids[i]) ;
      fn[28 + S6RC_FDHOLDER_LEN + len] = 0 ;
      auto_symlink(compiled, fn, fmt) ;
    }
  }

  if (!stralloc_cats(&satmp,
    "#!" EXECLINE_SHEBANGPREFIX "execlineb -P\n"
    EXECLINE_EXTBINPREFIX "pipeline -dw --\n{\n  "
    EXECLINE_EXTBINPREFIX "if -n --\n  {\n    "
    EXECLINE_EXTBINPREFIX "forstdin -x 1 -- i\n    "
    EXECLINE_EXTBINPREFIX "exit 1\n  }\n  "
    EXECLINE_EXTBINPREFIX "if -nt --\n  {\n    "
    S6_EXTBINPREFIX "s6-ipcclient -l0 -- s\n    "
    S6RC_LIBEXECPREFIX "s6-rc-fdholder-filler -1 -- ")
   || !write_pipelines(&satmp, db)
   || !stralloc_cats(&satmp, "\n  }\n  "
    S6_EXTBINPREFIX "s6-svc -t .\n}\n")) dienomem() ;
  if (fdhuser)
  {
    if (!stralloc_cats(&satmp, S6_EXTBINPREFIX "s6-envuidgid -i -- ")
     || !string_quote(&satmp, fdhuser, str_len(fdhuser))
     || !stralloc_catb(&satmp, "\n", 1)) dienomem() ;
  }
  if (!stralloc_cats(&satmp, S6_EXTBINPREFIX "s6-fdholder-daemon -1 ")) dienomem() ;
  if (fdhuser)
  {
    if (!stralloc_cats(&satmp, "-U ")) dienomem() ;
  }
  if (!stralloc_cats(&satmp, "-i data/rules -- s\n")) dienomem() ;
  auto_file(compiled, "servicedirs/" S6RC_FDHOLDER "/run", satmp.s + base, satmp.len - base) ;
  satmp.len = base ;
  auto_rights(compiled, "servicedirs/" S6RC_FDHOLDER "/run", 0755) ;
}

static inline void write_specials (char const *compiled, s6rc_db_t const *db, uint64 const *uids, unsigned int uidn, gid_t const *gids, unsigned int gidn, char const *fdhuser)
{
  write_oneshot_runner(compiled, uids, uidn, gids, gidn) ;
  write_fdholder(compiled, db, uids, uidn, gids, gidn, fdhuser) ;
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

static int read_uint (char const *file, unsigned int *fd)
{
  char buf[UINT_FMT + 1] ;
  register int r = openreadnclose(file, buf, UINT_FMT) ;
  if (r < 0) return (errno == ENOENT) ? 0 : -1 ;
  buf[byte_chr(buf, r, '\n')] = 0 ;
  if (!uint0_scan(buf, fd)) return (errno = EINVAL, -1) ;
  return 1 ;
}

static inline void write_run_wrapper (char const *compiled, char const *fn, s6rc_db_t const *db, unsigned int i, unsigned int fd)
{
  unsigned int base = satmp.len ;
  if (!stralloc_cats(&satmp, "#!" EXECLINE_SHEBANGPREFIX "execlineb -P\n")) dienomem() ;
  if (db->services[i].x.longrun.pipeline[0] < db->nlong)
  {
    if (!stralloc_cats(&satmp, S6_EXTBINPREFIX "s6-fdholder-retrieve ../s6rc-fdholder/s \"pipe:s6rc-r-")
     || !string_quote_nodelim(&satmp, db->string + db->services[i].name, str_len(db->string + db->services[i].name))
     || !stralloc_cats(&satmp, "\"\n")) dienomem() ;
  }
  if (db->services[i].x.longrun.pipeline[1] < db->nlong)
  {
    char const *consumername = db->string + db->services[db->services[i].x.longrun.pipeline[1]].name ;
    if (!stralloc_cats(&satmp, EXECLINE_EXTBINPREFIX "fdmove ")
     || !stralloc_cats(&satmp, fd == 3 ? "4" : "3")
     || !stralloc_cats(&satmp, " 0\n"
      S6_EXTBINPREFIX "s6-fdholder-retrieve ../s6rc-fdholder/s \"pipe:s6rc-w-")
     || !string_quote_nodelim(&satmp, consumername, str_len(consumername))
     || !stralloc_cats(&satmp, "\"\n"
      EXECLINE_EXTBINPREFIX "fdmove 1 0\n"
      EXECLINE_EXTBINPREFIX "fdmove 0 ")
     || !stralloc_cats(&satmp, fd == 3 ? "4" : "3")
     || !stralloc_cats(&satmp, "\n")) dienomem() ;
  }
  if (!stralloc_cats(&satmp, "./run.user\n")) dienomem() ;
  auto_file(compiled, fn, satmp.s + base, satmp.len - base) ;
  satmp.len = base ;
  auto_rights(compiled, fn, 0755) ;
}

static inline void write_servicedirs (char const *compiled, s6rc_db_t const *db, char const *const *srcdirs)
{
  unsigned int clen = str_len(compiled) ;
  unsigned int i = 2 ;
  if (verbosity >= 3) strerr_warni3x("writing ", compiled, "/servicedirs") ;
  for (; i < db->nlong ; i++)
  {
    struct stat st ;
    unsigned int srcdirlen = str_len(srcdirs[i]) ;
    unsigned int len = str_len(db->string + db->services[i].name) ;
    unsigned int fd = 0 ;
    register int r ;
    char srcfn[srcdirlen + len + 18] ;
    char dstfn[clen + len + 30] ;
    byte_copy(dstfn, clen, compiled) ;
    byte_copy(dstfn + clen, 13, "/servicedirs/") ;
    byte_copy(dstfn + clen + 13, len + 1, db->string + db->services[i].name) ;
    if (mkdir(dstfn, 0755) < 0)
    {
      cleanup(compiled) ;
      strerr_diefu2sys(111, "mkdir ", dstfn) ;
    }
    byte_copy(srcfn, srcdirlen, srcdirs[i]) ;
    srcfn[srcdirlen] = '/' ;
    byte_copy(srcfn + srcdirlen + 1, len, db->string + db->services[i].name) ;
    byte_copy(srcfn + srcdirlen + 1 + len, 17, "/notification-fd") ;
    r = read_uint(srcfn, &fd) ;
    if (r < 0)
    {
      cleanup(compiled) ;
      strerr_diefu2sys(111, "read ", srcfn) ;
    }
    if (r)
    {
      char fmt[UINT_FMT] ;
      unsigned int fmtlen = uint_fmt(fmt, fd) ;
      fmt[fmtlen++] = '\n' ;
      byte_copy(dstfn + clen + 13 + len, 17, "/notification-fd") ;
      if (!openwritenclose_unsafe(dstfn, fmt, fmtlen))
      {
        cleanup(compiled) ;
        strerr_diefu2sys(111, "write to ", dstfn) ;
      }
    }

    byte_copy(dstfn + clen + 13 + len, 5, "/run") ;
    if (db->services[i].x.longrun.pipeline[0] < db->nlong || db->services[i].x.longrun.pipeline[1] < db->nlong)
    {
      write_run_wrapper(compiled, dstfn + clen + 1, db, i, fd) ;
      byte_copy(dstfn + clen + 17 + len, 6, ".user") ;
    }
    byte_copy(srcfn + srcdirlen + 1 + len, 5, "/run") ;
    if (!filecopy(srcfn, dstfn, 0755))
    {
      cleanup(compiled) ;
      strerr_diefu4sys(111, "copy ", srcfn, " to ", dstfn) ;
    }
    byte_copy(dstfn + clen + 14 + len, 7, "finish") ;
    byte_copy(srcfn + srcdirlen + len + 2, 7, "finish") ;
    filecopy(srcfn, dstfn, 0755) ;
    byte_copy(dstfn + clen + 14 + len, 15, "timeout-finish") ;
    byte_copy(srcfn + srcdirlen + len + 2, 15, "timeout-finish") ;
    filecopy(srcfn, dstfn, 0644) ;

    byte_copy(srcfn + srcdirlen + len + 2, 9, "nosetsid") ;
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
      byte_copy(dstfn + clen + 14 + len, 9, "nosetsid") ;
      fd = open_trunc(dstfn) ;
      if (fd < 0)
      {
        cleanup(compiled) ;
        strerr_diefu2sys(111, "touch ", dstfn) ;
      }
      close(fd) ;
    }

    byte_copy(dstfn + clen + 14 + len, 5, "data") ;
    byte_copy(srcfn + srcdirlen + len + 2, 5, "data") ;
    dircopy(compiled, srcfn, dstfn) ;
    byte_copy(dstfn + clen + 14 + len, 4, "env") ;
    byte_copy(srcfn + srcdirlen + len + 2, 4, "env") ;
    dircopy(compiled, srcfn, dstfn) ;
  }
}

static inline int write_service (buffer *b, s6rc_service_t const *sv, int type)
{
  char pack[49] ;
  unsigned int m ;
  uint32_pack_big(pack, sv->name) ;
  uint32_pack_big(pack + 4, sv->flags) ;
  uint32_pack_big(pack + 8, sv->timeout[0]) ;
  uint32_pack_big(pack + 12, sv->timeout[1]) ;
  uint32_pack_big(pack + 16, sv->ndeps[0]) ;
  uint32_pack_big(pack + 20, sv->ndeps[1]) ;
  uint32_pack_big(pack + 24, sv->deps[0]) ;
  uint32_pack_big(pack + 28, sv->deps[1]) ;
  if (type)
  {
    uint32_pack_big(pack + 32, sv->x.longrun.pipeline[0]) ;
    uint32_pack_big(pack + 36, sv->x.longrun.pipeline[1]) ;
    m = 40 ;
  }
  else
  {
    uint32_pack_big(pack + 32, sv->x.oneshot.argc[0]) ;
    uint32_pack_big(pack + 36, sv->x.oneshot.argv[0]) ;
    uint32_pack_big(pack + 40, sv->x.oneshot.argc[1]) ;
    uint32_pack_big(pack + 44, sv->x.oneshot.argv[1]) ;
    m = 48 ;
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
    unsigned int i = db->nlong ;
    s6rc_service_t const *sv = db->services ;
    while (i--) if (!write_service(&b, sv++, 1)) goto err ;
    i = db->nshort ;
    while (i--) if (!write_service(&b, sv++, 0)) goto err ;
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

static inline void write_compiled (
  char const *compiled,
  s6rc_db_t const *db,
  char const *const *srcdirs,
  bundle_t const *bundles,
  unsigned int nbundles,
  uint32 const *bdeps,
  uint64 const *uids,
  unsigned int uidn,
  gid_t const *gids,
  unsigned int gidn,
  char const *fdhuser)
{
  if (verbosity >= 2) strerr_warni2x("writing compiled information to ", compiled) ;
  init_compiled(compiled) ;
  write_sizes(compiled, db) ;
  write_resolve(compiled, db, bundles, nbundles, bdeps) ;
  stralloc_free(&data) ;
  write_db(compiled, db) ;
  write_specials(compiled, db, uids, uidn, gids, gidn, fdhuser) ;
  write_servicedirs(compiled, db, srcdirs) ;
}

int main (int argc, char const *const *argv)
{
  before_t before = BEFORE_ZERO ;
  char const *compiled ;
  char const *fdhuser = 0 ;
  unsigned int uidn = 0, gidn = 0 ;
  uint64 uids[256] ;
  gid_t gids[256] ;
  PROG = "s6-rc-compile" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      register int opt = subgetopt_r(argc, argv, "v:u:g:h:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 'u' : if (!uint64_scanlist(uids, 255, l.arg, &uidn)) dieusage() ; break ;
        case 'g' : if (!gid_scanlist(gids, 255, l.arg, &gidn)) dieusage() ; break ;
        case 'h' : fdhuser = l.arg ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
  }
  if (argc < 2) dieusage() ;
  if (!uidn && !gidn) uids[uidn++] = 0 ;
  compiled = *argv++ ;
  before.specialdeps[0] = add_internal_longrun(&before, S6RC_ONESHOT_RUNNER) ;
  before.specialdeps[1] = add_internal_longrun(&before, S6RC_FDHOLDER) ;
  for (; *argv ; argv++) add_sources(&before, *argv) ;
  add_pipeline_bundles(&before) ;

  {
    unsigned int n = genalloc_len(oneshot_t, &before.oneshots) + genalloc_len(longrun_t, &before.longruns) ;
    unsigned int nbits = bitarray_div8(n) ;
    unsigned int nbundles = genalloc_len(bundle_t, &before.bundles) ;
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
    unsigned int nbdeps = resolve_bundles(genalloc_s(bundle_t, &before.bundles), bundles, db.nshort, db.nlong, nbundles, genalloc_s(unsigned int, &before.indices), barray) ;
    uint32 bdeps[nbdeps] ;

    genalloc_free(bundle_t, &before.bundles) ;
    flatlist_bundles(bundles, nbundles, nbits, barray, bdeps) ;
    db.ndeps = resolve_services(&db, &before, srcdirs, sarray, barray) ;
    genalloc_free(unsigned int, &before.indices) ;
    genalloc_free(oneshot_t, &before.oneshots) ;
    genalloc_free(longrun_t, &before.longruns) ;
    genalloc_free(nameinfo_t, &nameinfo) ;
    avltree_free(&names_map) ;
    {
      uint32 deps[db.ndeps << 1] ;
      db.deps = deps ;
      flatlist_services(&db, sarray) ;
      write_compiled(compiled, &db, srcdirs, bundles, nbundles, bdeps, uids, uidn, gids, gidn, fdhuser) ;
    }
  }

  return 0 ;
}
