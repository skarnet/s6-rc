/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <skalibs/types.h>
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

#define USAGE "s6-rc-compile [ -v verbosity ] [ -u okuid,okuid... ] [ -g okgid,okgid... ] [ -h fdholder_user ] [ -b ] destdir sources..."
#define dieusage() strerr_dieusage(100, USAGE)
#define dienomem() strerr_dief1x(111, "out of memory") ;

#define S6RC_INTERNALS "s6-rc-compile internals"

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
  return strcmp((char const *)a, (char const *)b) ;
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
  uint32_t annotation_flags ;
  uint32_t timeout[2] ;
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
  unsigned int nproducers ;
  unsigned int prodindex ; /* pos in indices */
  unsigned int consumer ; /* pos in data */
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
  uint32_t specialdeps[2] ;
  uint32_t nproducers ;
} ;

#define BEFORE_ZERO { .indices = GENALLOC_ZERO, .oneshots = GENALLOC_ZERO, .longruns = GENALLOC_ZERO, .bundles = GENALLOC_ZERO, .nargvs = 0, .specialdeps = { 0, 0 }, .nproducers = 0 } ;


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
  uint32_t id ;
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
    size_t namelen = strlen(name) ;
    uint32_t i = genalloc_len(nameinfo_t, &nameinfo) ;
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
  if (!memcmp(s, "s6rc-", 5) && !memcmp(s, "s6-rc-", 6))
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
    .nproducers = 0,
    .prodindex = 0,
    .consumer = 0,
    .pipelinename = 0
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
  size_t start = satmp.len ;
  int cont = 1 ;
  char buf[2048] ;
  int fd = open_readatb(dirfd, list) ;
  if (fd < 0) return 0 ;
  buffer_init(&b, &buffer_read, fd, buf, 2048) ;
  *listindex = genalloc_len(unsigned int, &be->indices) ;
  while (cont)
  {
    int r = skagetln(&b, &satmp, '\n') ;
    if (!r) cont = 0 ;
    else
    {
      size_t i = start ;
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
    buffer b = BUFFER_INIT(&buffer_read, fd, buf, 4096) ;
    r = el_parse_from_buffer(&keep, &b) ;
    switch (r)
    {
      case -4 : strerr_dief8x(1, "syntax error in ", srcdir, "/", name, "/", script, ": unmatched ", "}");
      case -3 : strerr_dief8x(1, "syntax error in ", srcdir, "/", name, "/", script, ": unmatched ", "{");
      case -2 : strerr_dief6x(1, "syntax error in ", srcdir, "/", name, "/", script) ;
      case -1 : strerr_diefu6sys(111, "parse ", srcdir, "/", name, "/", script) ;
    }
    close(fd) ;
  }
  *argc = r ;
  be->nargvs += r+1 ;
}

static uint32_t read_timeout (int dirfd, char const *srcdir, char const *name, char const *tname)
{
  char buf[64] ;
  uint32_t timeout = 0 ;
  size_t r = openreadnclose_at(dirfd, tname, buf, 63) ;
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
  longrun_t service = { .srcdir = srcdir, .nproducers = 0, .prodindex = 0, .consumer = 0, .pipelinename = 0 } ;
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
    service.consumer = genalloc_s(unsigned int, &be->indices)[relatedindex] ;
    if (!genalloc_append(unsigned int, &be->indices, &be->specialdeps[1])) dienomem() ;
    service.common.ndeps += 2 ;
    if (verbosity >= 3)
      strerr_warni3x(name, " is a producer for ", data.s + service.consumer) ;
    fd = 1 ;
  }
  if (add_namelist(be, dirfd, srcdir, name, "consumer-for", &service.prodindex, &service.nproducers) && service.nproducers)
  {
    be->nproducers += service.nproducers ;
    if (!fd)
    {
      unsigned int prod0 = genalloc_s(unsigned int, &be->indices)[service.prodindex] ;
      if (!genalloc_append(unsigned int, &be->indices, &prod0)) dienomem() ;
      genalloc_s(unsigned int, &be->indices)[service.prodindex++] = be->specialdeps[1] ;
      service.common.ndeps++ ;
      fd = 2 ;
    }
    if (verbosity >= 3)
      for (unsigned int i = 0 ; i < service.nproducers ; i++)
        strerr_warni3x(name, " is a consumer for ", data.s + genalloc_s(unsigned int, &be->indices)[service.prodindex + i]) ;
  }
  if (fd == 2)
  {
    if (add_namelist(be, dirfd, srcdir, name, "pipeline-name", &relatedindex, &n))
    {
      if (n != 1)
        strerr_dief5x(1, srcdir, "/", name, "/pipeline-name", " should only contain one name") ;
      service.pipelinename = genalloc_s(unsigned int, &be->indices)[relatedindex] ;
      genalloc_setlen(unsigned int, &be->indices, relatedindex) ;
    }
  }
  else if (faccessat(dirfd, "pipeline-name", F_OK, 0) < 0)
  {
    if (errno != ENOENT)
      strerr_diefu5sys(111, "access ", srcdir, "/", name, "/pipeline-name") ;
  }
  else if (verbosity)
    strerr_warnw4x(srcdir, "/", name, " contains a pipeline-name file despite not being a last consumer; this file will be ignored") ;
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
  size_t r ;
  if (verbosity >= 2) strerr_warni4x("parsing ", srcdir, "/", name) ;
  r = openreadnclose_at(dirfd, "type", typestr, 8) ;
  if (!r)
  {
    if (!errno) errno = EINVAL ;
    strerr_diefu5sys(111, "read ", srcdir, "/", name, "/type") ;
  }
  if (typestr[r-1] == '\n') r-- ;
  typestr[r++] = 0 ;
  if (!strcmp(typestr, "oneshot")) add_oneshot(be, dirfd, srcdir, name) ;
  else if (!strcmp(typestr, "longrun")) add_longrun(be, dirfd, srcdir, name) ;
  else if (!strcmp(typestr, "bundle")) add_bundle(be, dirfd, srcdir, name) ;
  else strerr_dief6x(1, "invalid ", srcdir, "/", name, "/type", ": must be oneshot, longrun, or bundle") ;
}

static inline void add_sources (before_t *be, char const *srcdir)
{
  size_t start = satmp.len ;
  size_t cur ;
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
    if (strchr(d->d_name, '\n'))
      strerr_dief3x(1, "subdirectory of ", srcdir, " contains a newline character") ;
    check_identifier(srcdir, d->d_name) ;
    satmp.len = cur ;
    if (!stralloc_catb(&satmp, d->d_name, strlen(d->d_name) + 1)) dienomem() ;
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

struct pipeline_recinfo_s
{
  before_t *be ;
  longrun_t const *longruns ;
  unsigned int nlong ;
  bundle_t bundle ;
  unsigned int pipelinename ;
} ;

static void add_tree_to_bundle_rec (struct pipeline_recinfo_s *recinfo, char const *name)
{
  nameinfo_t const *info ;
  {
    uint32_t id ;
    avltree_search(&names_map, name, &id) ;
    info = genalloc_s(nameinfo_t, &nameinfo) + id ;
  }
  if (info->type != SVTYPE_LONGRUN) return ; /* will error out later */
  if (recinfo->bundle.n++ >= recinfo->nlong)
    strerr_dief4x(1, "pipeline ", data.s + recinfo->pipelinename, " is too long: possible loop involving ", name) ;
  if (verbosity >= 4)
    strerr_warni4x("adding ", name, " to pipeline ", data.s + recinfo->pipelinename) ;
  if (!genalloc_append(unsigned int, &recinfo->be->indices, &info->pos)) dienomem() ;
  for (unsigned int i = 0 ; i < recinfo->longruns[info->i].nproducers ; i++)
    add_tree_to_bundle_rec(recinfo, data.s + genalloc_s(unsigned int, &recinfo->be->indices)[recinfo->longruns[info->i].prodindex + i]) ;
}

static inline void add_pipeline_bundles (before_t *be)
{
  longrun_t const *longruns = genalloc_s(longrun_t, &be->longruns) ;
  unsigned int i = genalloc_len(longrun_t, &be->longruns) ;
  if (verbosity >= 2) strerr_warni1x("making bundles for pipelines") ;
  while (i--) if (longruns[i].pipelinename)
  {
    struct pipeline_recinfo_s recinfo =
    {
      .be = be,
      .longruns = longruns,
      .nlong = genalloc_len(longrun_t, &be->longruns),
      .bundle = { .listindex = genalloc_len(unsigned int, &be->indices), .n = 0 },
      .pipelinename = longruns[i].pipelinename
    } ;
    if (verbosity >= 3)
      strerr_warni2x("creating bundle for pipeline ", data.s + recinfo.pipelinename) ;
    {
      uint32_t dummy ;
      add_name(be, S6RC_INTERNALS, data.s + recinfo.pipelinename, SVTYPE_BUNDLE, &recinfo.bundle.name, &dummy) ;
    }
    add_tree_to_bundle_rec(&recinfo, keep.s + longruns[i].common.kname) ;
    if (!genalloc_append(bundle_t, &be->bundles, &recinfo.bundle)) dienomem() ;
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
      uint32_t id ;
      nameinfo_t const *p ;
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
  memset(barray, 0, recinfo.nbits * nbundles) ;
  memset(mark, 0, nbundles) ;
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

static inline void flatlist_bundles (bundle_t *bundles, unsigned int nbundles, unsigned int nbits, unsigned char const *barray, uint32_t *bdeps)
{
  unsigned int i = 0 ;
  if (verbosity >= 3) strerr_warni1x("converting bundle array") ;
  for (; i < nbundles ; i++)
  {
    unsigned char const *mybits = barray + i * nbits ;
    uint32_t *mydeps = bdeps + bundles[i].listindex ;
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
    uint32_t id ;
    nameinfo_t const *p ;
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

static inline uint32_t resolve_prodcons (s6rc_longrun_t *l, longrun_t const *longruns, unsigned int const *indices, unsigned int n, uint32_t prodindex, uint32_t nlong, uint32_t *prodlist)
{
  if (longruns[n].consumer)
  {
    unsigned int i = 0 ;
    uint32_t j ;
    nameinfo_t const *p ;
    avltree_search(&names_map, data.s + longruns[n].consumer, &j) ;
    p = genalloc_s(nameinfo_t, &nameinfo) + j ;
    if (p->type != SVTYPE_LONGRUN)
      strerr_dief6x(1, "longrun service ", keep.s + longruns[n].common.kname, " declares being a producer for a service named ", data.s + p->pos, " of type ", typestr(p->type)) ;
    for (; i < longruns[p->i].nproducers ; i++)
    {
      uint32_t k ;
      nameinfo_t const *q ;
      avltree_search(&names_map, data.s + indices[longruns[p->i].prodindex + i], &k) ;
      q = genalloc_s(nameinfo_t, &nameinfo) + k ;
      if (q->type != SVTYPE_LONGRUN)
        strerr_dief6x(1, "longrun service ", data.s + p->pos, " declares a consumer ", data.s + q->pos, " of type ", typestr(q->type)) ;
      if (q->i == n) break ;
    }
    if (i == longruns[p->i].nproducers)
      strerr_dief5x(1, "longrun service ", keep.s + longruns[n].common.kname, " declares being a producer for a service named ", data.s + p->pos, " that does not declare it back") ;
    l->consumer = p->i ; 
  }
  else l->consumer = nlong ;

  for (unsigned int i = 0 ; i < longruns[n].nproducers ; i++)
  {
    uint32_t j, k ;
    nameinfo_t const *p ;
    nameinfo_t const *q ;
    avltree_search(&names_map, data.s + indices[longruns[n].prodindex + i], &j) ;
    p = genalloc_s(nameinfo_t, &nameinfo) + j ;
    if (p->type != SVTYPE_LONGRUN)
        strerr_dief6x(1, "longrun service ", keep.s + longruns[n].common.kname, " declares being the consumer for a service named ", data.s + p->pos, " of type ", typestr(p->type)) ;
    avltree_search(&names_map, data.s + longruns[p->i].consumer, &k) ;
    q = genalloc_s(nameinfo_t, &nameinfo) + k ;
    if (q->type != SVTYPE_LONGRUN)
      strerr_dief6x(1, "longrun service ", data.s + p->pos, " declares a producer ", data.s + q->pos, " of type ", typestr(q->type)) ;
    if (q->i != n)
      strerr_dief5x(1, "longrun service ", keep.s + longruns[n].common.kname, " declares being the consumer for a service named ", data.s + p->pos, " that does not declare it back") ;
    prodlist[prodindex + i] = p->i ;
  }
  l->producers = prodindex ;
  l->nproducers = longruns[n].nproducers ;
  return longruns[n].nproducers ;
}

static inline unsigned int ugly_bitarray_vertical_countones (unsigned char const *sarray, unsigned int n, unsigned int i)
{
  unsigned int m = 0, j = 0, nbits = bitarray_div8(n) ;
  for (; j < n ; j++)
    if (bitarray_peek(sarray + j * nbits, i)) m++ ;
  return m ;
}

static inline void resolve_services (s6rc_db_t *db, before_t const *be, char const **srcdirs, unsigned char *sarray, unsigned char const *barray)
{
  oneshot_t const *oneshots = genalloc_s(oneshot_t const, &be->oneshots) ;
  longrun_t const *longruns = genalloc_s(longrun_t const, &be->longruns) ;
  unsigned int const *indices = genalloc_s(unsigned int const, &be->indices) ;
  unsigned int n = db->nshort + db->nlong ;
  unsigned int nbits = bitarray_div8(n) ;
  uint32_t prodindex = 0 ;
  uint32_t i = 0 ;
  uint32_t total[2] = { 0, 0 } ;
  if (verbosity >= 2) strerr_warni1x("resolving service names") ;
  memset(sarray, 0, nbits * n) ;
  for (; i < db->nlong ; i++)
  {
    srcdirs[i] = longruns[i].srcdir ;
    db->services[i].name = longruns[i].common.kname ;
    db->services[i].flags = longruns[i].common.annotation_flags ;
    db->services[i].timeout[0] = longruns[i].common.timeout[0] ;
    db->services[i].timeout[1] = longruns[i].common.timeout[1] ;
    prodindex += resolve_prodcons(&db->services[i].x.longrun, longruns, indices, i, prodindex, db->nlong, db->producers) ;
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

  if (total[0] != total[1])
    strerr_dief1x(101, "database inconsistency: dependencies and reverse dependencies do not match. Please submit a bug-report.") ;
  db->ndeps = total[1] ;
}

static inline void flatlist_services (s6rc_db_t *db, unsigned char const *sarray)
{
  unsigned int n = db->nshort + db->nlong ;
  unsigned int nbits = bitarray_div8(n) ;
  diuint32 problem ;
  unsigned int i = 0 ;
  int r ;
  if (verbosity >= 3) strerr_warni1x("converting service dependency array") ;
  for (; i < n ; i++)
  {
    uint32_t *mydeps = db->deps + db->services[i].deps[0] ;
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
  size_t clen = strlen(compiled) ;
  size_t dlen = strlen(dir) ;
  char fn[clen + dlen + 2] ;
  memcpy(fn, compiled, clen) ;
  fn[clen] = dlen ? '/' : 0 ;
  memcpy(fn + clen + 1, dir, dlen + 1) ;
  if (mkdir(fn, 0755) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "mkdir ", fn) ;
  }
}

static void auto_file (char const *compiled, char const *file, char const *s, unsigned int n)
{
  size_t clen = strlen(compiled) ;
  size_t flen = strlen(file) ;
  char fn[clen + flen + 2] ;
  memcpy(fn, compiled, clen) ;
  fn[clen] = '/' ;
  memcpy(fn + clen + 1, file, flen + 1) ;
  if (!openwritenclose_unsafe(fn, s, n))
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "write to ", fn) ;
  }
}

static void auto_symlink (char const *compiled, char const *name, char const *target)
{
  size_t clen = strlen(compiled) ;
  size_t flen = strlen(name) ;
  char fn[clen + flen + 2] ;
  memcpy(fn, compiled, clen) ;
  fn[clen] = '/' ;
  memcpy(fn + clen + 1, name, flen + 1) ;
  if (symlink(target, fn) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu4sys(111, "symlink ", target, " to ", fn) ;
  }
}

static void auto_rights (char const *compiled, char const *file, mode_t mode)
{
  size_t clen = strlen(compiled) ;
  size_t flen = strlen(file) ;
  char fn[clen + flen + 2] ;
  memcpy(fn, compiled, clen) ;
  fn[clen] = '/' ;
  memcpy(fn + clen + 1, file, flen + 1) ;
  if (chmod(fn, mode) < 0)
  {
    cleanup(compiled) ;
    strerr_diefu2sys(111, "chmod ", fn) ;
  }
}

static inline void init_compiled (char const *compiled)
{
  int compiledlock ;
  if (mkdir(compiled, 0755) < 0)
    strerr_diefu2sys(111, "mkdir ", compiled) ;
  if (!s6rc_lock(0, 0, 0, compiled, 2, &compiledlock, 0))
    strerr_diefu2sys(111, "take lock on ", compiled) ;
  auto_dir(compiled, "servicedirs") ;
}

static inline void write_sizes (char const *compiled, s6rc_db_t const *db)
{
  char pack[24] ;
  if (verbosity >= 3) strerr_warni3x("writing ", compiled, "/n") ;
  uint32_pack_big(pack, (uint32_t)db->nshort) ;
  uint32_pack_big(pack + 4, (uint32_t)db->nlong) ;
  uint32_pack_big(pack + 8, (uint32_t)db->stringlen) ;
  uint32_pack_big(pack + 12, (uint32_t)db->nargvs) ;
  uint32_pack_big(pack + 16, (uint32_t)db->ndeps) ;
  uint32_pack_big(pack + 20, (uint32_t)db->nproducers) ;
  auto_file(compiled, "n", pack, 24) ;
}

static void make_skel (char const *compiled, char const *name, uid_t const *uids, size_t uidn, gid_t const *gids, size_t gidn, unsigned int notif)
{
  size_t namelen = strlen(name) ;
  char fmt[UINT_FMT] ;
  size_t i = uint_fmt(fmt, notif) ;
  fmt[i++] = '\n' ;
  char fn[namelen + 29] ;
  memcpy(fn, "servicedirs/", 12) ;
  memcpy(fn + 12, name, namelen + 1) ;
  auto_dir(compiled, fn) ;
  memcpy(fn + 12 + namelen, "/notification-fd", 17) ;
  auto_file(compiled, fn, fmt, i) ;
  memcpy(fn + 13 + namelen, "data", 5) ;
  auto_dir(compiled, fn) ;
  memcpy(fn + 17 + namelen, "/rules", 7) ;
  auto_dir(compiled, fn) ;
  if (gidn)
  {
    memcpy(fn + 23 + namelen, "/gid", 5) ;
    auto_dir(compiled, fn) ;
  }
  memcpy(fn + 23 + namelen, "/uid", 5) ;
  auto_dir(compiled, fn) ;
}

static inline void write_oneshot_runner (char const *compiled, uid_t const *uids, size_t uidn, gid_t const *gids, size_t gidn, int blocking)
{
  size_t base = satmp.len ;
  size_t i ;
  char fn[35 + sizeof(S6RC_ONESHOT_RUNNER)] = "servicedirs/" S6RC_ONESHOT_RUNNER "/data/rules/gid/" ;
  make_skel(compiled, S6RC_ONESHOT_RUNNER, uids, uidn, gids, gidn, 3) ;
  if (gidn)
  {
    i = gidn ;
    while (i--)
    {
      size_t len = gid_fmt(fn + 28 + S6RC_ONESHOT_RUNNER_LEN, gids[i]) ;
      fn[28 + S6RC_ONESHOT_RUNNER_LEN + len] = 0 ;
      auto_dir(compiled, fn) ;
      memcpy(fn + 28 + S6RC_ONESHOT_RUNNER_LEN + len, "/allow", 7) ;
      auto_file(compiled, fn, "", 0) ;
    }
  }
  fn[24 + S6RC_ONESHOT_RUNNER_LEN] = 'u' ;
  i = uidn ;
  while (i--)
  {
    size_t len = uid_fmt(fn + 28 + S6RC_ONESHOT_RUNNER_LEN, uids[i]) ;
    fn[28 + S6RC_ONESHOT_RUNNER_LEN + len] = 0 ;
    auto_dir(compiled, fn) ;
    memcpy(fn + 28 + S6RC_ONESHOT_RUNNER_LEN + len, "/allow", 7) ;
    auto_file(compiled, fn, "", 0) ;
  }
  if (!stralloc_cats(&satmp, "#!"
    EXECLINE_SHEBANGPREFIX "execlineb -P\n"
    EXECLINE_EXTBINPREFIX "fdmove -c 2 1\n"
    EXECLINE_EXTBINPREFIX "fdmove 1 3\n"
    S6_EXTBINPREFIX "s6-ipcserver-socketbinder -- s\n"
    S6_EXTBINPREFIX "s6-ipcserverd -1 --\n"
    S6_EXTBINPREFIX "s6-ipcserver-access -v0 -E -l0 -i data/rules --\n"
    S6_EXTBINPREFIX "s6-sudod -t 30000 --\n"
    S6RC_EXTLIBEXECPREFIX "s6-rc-oneshot-run -l ../.. ")
   || (blocking && !stralloc_cats(&satmp, "-b "))
   || !stralloc_cats(&satmp, "--\n")) dienomem() ;
  auto_file(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/run", satmp.s + base, satmp.len - base) ;
  satmp.len = base ;
  auto_rights(compiled, "servicedirs/" S6RC_ONESHOT_RUNNER "/run", 0755) ;
}

static inline void write_fdholder (char const *compiled, s6rc_db_t const *db, uid_t const *uids, size_t uidn, gid_t const *gids, size_t gidn, char const *fdhuser)
{
  size_t base = satmp.len ;
  make_skel(compiled, S6RC_FDHOLDER, uids, uidn, gids, gidn, 1) ;
  {
    char fn[62 + S6RC_FDHOLDER_LEN + UID_FMT] = "servicedirs/" S6RC_FDHOLDER "/data/rules/uid/" ;
    char fmt[7 + UID_FMT] = "../uid/" ;
    size_t i = uid_fmt(fmt + 7, uids[0]) ;
    fmt[7 + i] = 0 ;
    memcpy(fn + 28 + S6RC_FDHOLDER_LEN, fmt + 7, i + 1) ;
    auto_dir(compiled, fn) ;
    memcpy(fn + 28 + S6RC_FDHOLDER_LEN + i, "/allow", 7) ;
    auto_file(compiled, fn, "", 0) ;
    memcpy(fn + 29 + S6RC_FDHOLDER_LEN + i, "env", 4) ;
    auto_dir(compiled, fn) ;
    memcpy(fn + 32 + S6RC_FDHOLDER_LEN + i, "/S6_FDHOLDER_LIST", 18) ;
    auto_file(compiled, fn, "\n", 1) ;
    memcpy(fn + 45 + S6RC_FDHOLDER_LEN + i, "STORE_REGEX", 12) ;
    auto_file(compiled, fn, "^pipe:s6rc-\n", 12) ;
    memcpy(fn + 45 + S6RC_FDHOLDER_LEN + i, "RETRIEVE_REGEX", 15) ;
    auto_symlink(compiled, fn, "S6_FDHOLDER_STORE_REGEX") ;
    memcpy(fn + 45 + S6RC_FDHOLDER_LEN + i, "SETDUMP", 8) ;
    auto_file(compiled, fn, "\n", 1) ;
    fn[45 + S6RC_FDHOLDER_LEN + i] = 'G' ;
    auto_file(compiled, fn, "\n", 1) ;
    
    for (i = 1 ; i < uidn ; i++)
    {
      size_t len = uid_fmt(fn + 28 + S6RC_FDHOLDER_LEN, uids[i]) ;
      fn[28 + S6RC_FDHOLDER_LEN + len] = 0 ;
      auto_symlink(compiled, fn, fmt + 7) ;
    }
    fn[24 + S6RC_FDHOLDER_LEN] = 'g' ;
    i = gidn ;
    while (i--)
    {
      size_t len = gid_fmt(fn + 28 + S6RC_FDHOLDER_LEN, gids[i]) ;
      fn[28 + S6RC_FDHOLDER_LEN + len] = 0 ;
      auto_symlink(compiled, fn, fmt) ;
    }
  }

  for (uint32_t j = 0 ; j < db->nlong ; j++)
    if (db->services[j].x.longrun.nproducers)
      if (!stralloc_cats(&satmp, db->string + db->services[j].name)
       || !stralloc_catb(&satmp, "\n", 1)) dienomem() ;

  auto_file(compiled, "servicedirs/" S6RC_FDHOLDER "/data/autofilled", satmp.s + base, satmp.len - base) ;
  satmp.len = base ;

  if (!stralloc_cats(&satmp,
    "#!" EXECLINE_SHEBANGPREFIX "execlineb -P\n"
    EXECLINE_EXTBINPREFIX "pipeline -dw --\n{\n  "
    EXECLINE_EXTBINPREFIX "if -n --\n  {\n    "
    EXECLINE_EXTBINPREFIX "forstdin -x 1 -- i\n    "
    EXECLINE_EXTBINPREFIX "exit 1\n  }\n  "
    EXECLINE_EXTBINPREFIX "if -nt --\n  {\n    "
    EXECLINE_EXTBINPREFIX "redirfd -r 0 ./data/autofilled\n    "
    S6_EXTBINPREFIX "s6-ipcclient -l0 -- s\n    "
    S6RC_EXTLIBEXECPREFIX "s6-rc-fdholder-filler -1 --\n  }\n  "
    S6_EXTBINPREFIX "s6-svc -t .\n}\n")) dienomem() ;
  if (fdhuser)
  {
    if (!stralloc_cats(&satmp, S6_EXTBINPREFIX "s6-envuidgid -i -- ")
     || !string_quote(&satmp, fdhuser, strlen(fdhuser))
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

static inline void write_specials (char const *compiled, s6rc_db_t const *db, uid_t const *uids, size_t uidn, gid_t const *gids, size_t gidn, char const *fdhuser, int blocking)
{
  write_oneshot_runner(compiled, uids, uidn, gids, gidn, blocking) ;
  write_fdholder(compiled, db, uids, uidn, gids, gidn, fdhuser) ;
}

static inline void write_resolve (char const *compiled, s6rc_db_t const *db, bundle_t const *bundles, unsigned int nbundles, uint32_t const *bdeps)
{
  size_t clen = strlen(compiled) ;
  int fd ;
  struct cdb_make c = CDB_MAKE_ZERO ;
  unsigned int i = db->nshort + db->nlong ;
  char fn[clen + 13] ;
  if (verbosity >= 3) strerr_warni3x("writing ", compiled, "/resolve.cdb") ;
  memcpy(fn, compiled, clen) ;
  memcpy(fn + clen, "/resolve.cdb", 13) ;
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
    uint32_pack_big(pack, i) ;
    if (cdb_make_add(&c, db->string + db->services[i].name, strlen(db->string + db->services[i].name), pack, 4) < 0)
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
    if (cdb_make_add(&c, data.s + bundles[i].name, strlen(data.s + bundles[i].name), pack, bundles[i].n << 2) < 0)
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

static void write_exe_wrapper (char const *compiled, char const *fn, s6rc_db_t const *db, unsigned int i, char const *exe, int needargs)
{
  size_t base = satmp.len ;
  if (!stralloc_cats(&satmp, "#!" EXECLINE_SHEBANGPREFIX "execlineb -")
   || !stralloc_cats(&satmp, needargs ? "S0\n" : "P\n")) dienomem() ;
  if (db->services[i].x.longrun.nproducers)
  {
    if (!stralloc_cats(&satmp, S6_EXTBINPREFIX "s6-fdholder-retrieve ../s6rc-fdholder/s \"pipe:s6rc-r-")
     || !string_quote_nodelim(&satmp, db->string + db->services[i].name, strlen(db->string + db->services[i].name))
     || !stralloc_cats(&satmp, "\"\n")) dienomem() ;
  }
  if (db->services[i].x.longrun.consumer < db->nlong)
  {
    char const *consumername = db->string + db->services[db->services[i].x.longrun.consumer].name ;
    if (!stralloc_cats(&satmp, EXECLINE_EXTBINPREFIX "fdmove 1 0\n"
      S6_EXTBINPREFIX "s6-fdholder-retrieve ../s6rc-fdholder/s \"pipe:s6rc-w-")
     || !string_quote_nodelim(&satmp, consumername, strlen(consumername))
     || !stralloc_cats(&satmp, "\"\n"
      EXECLINE_EXTBINPREFIX "fdswap 0 1\n")) dienomem() ;
  }
  if (!stralloc_cats(&satmp, "./")
   || !stralloc_cats(&satmp, exe)
   || !stralloc_cats(&satmp, ".user")
   || !stralloc_cats(&satmp, needargs ? " $@\n" : "\n")) dienomem() ;
  auto_file(compiled, fn, satmp.s + base, satmp.len - base) ;
  satmp.len = base ;
  auto_rights(compiled, fn, 0755) ;
}

static inline void write_servicedirs (char const *compiled, s6rc_db_t const *db, char const *const *srcdirs)
{
  size_t clen = strlen(compiled) ;
  unsigned int i = 2 ;
  if (verbosity >= 3) strerr_warni3x("writing ", compiled, "/servicedirs") ;
  for (; i < db->nlong ; i++)
  {
    size_t srcdirlen = strlen(srcdirs[i]) ;
    size_t len = strlen(db->string + db->services[i].name) ;
    unsigned int fd = 0 ;
    int ispipelined = db->services[i].x.longrun.nproducers || db->services[i].x.longrun.consumer < db->nlong ;
    int r ;
    char srcfn[srcdirlen + len + 18] ;
    char dstfn[clen + len + 30] ;
    memcpy(dstfn, compiled, clen) ;
    memcpy(dstfn + clen, "/servicedirs/", 13) ;
    memcpy(dstfn + clen + 13, db->string + db->services[i].name, len + 1) ;
    if (mkdir(dstfn, 0755) < 0)
    {
      cleanup(compiled) ;
      strerr_diefu2sys(111, "mkdir ", dstfn) ;
    }
    memcpy(srcfn, srcdirs[i], srcdirlen) ;
    srcfn[srcdirlen] = '/' ;
    memcpy(srcfn + srcdirlen + 1, db->string + db->services[i].name, len) ;
    memcpy(srcfn + srcdirlen + 1 + len, "/notification-fd", 17) ;
    r = s6rc_read_uint(srcfn, &fd) ;
    if (r < 0)
    {
      cleanup(compiled) ;
      strerr_diefu2sys(111, "read ", srcfn) ;
    }
    if (r)
    {
      char fmt[UINT_FMT] ;
      size_t fmtlen = uint_fmt(fmt, fd) ;
      fmt[fmtlen++] = '\n' ;
      memcpy(dstfn + clen + 13 + len, "/notification-fd", 17) ;
      if (!openwritenclose_unsafe(dstfn, fmt, fmtlen))
      {
        cleanup(compiled) ;
        strerr_diefu2sys(111, "write to ", dstfn) ;
      }
      if (fd < 3 && verbosity)
      {
        fmt[fmtlen-1] = 0 ;
        strerr_warnw4x("longrun ", db->string + db->services[i].name, " has a notification-fd of ", fmt) ;
      }
    }

    memcpy(srcfn + srcdirlen + 1 + len, "/run", 5) ;
    memcpy(dstfn + clen + 13 + len, "/run", 5) ;
    if (ispipelined)
    {
      write_exe_wrapper(compiled, dstfn + clen + 1, db, i, "run", 0) ;
      memcpy(dstfn + clen + 17 + len, ".user", 6) ;
    }
    if (!filecopy_unsafe(srcfn, dstfn, 0755))
    {
      cleanup(compiled) ;
      strerr_diefu4sys(111, "copy ", srcfn, " to ", dstfn) ;
    }

    memcpy(srcfn + srcdirlen + len + 2, "finish", 7) ;
    if (access(srcfn, R_OK) == 0)
    {
      memcpy(dstfn + clen + 14 + len, "finish", 7) ;
      if (ispipelined)
      {
        write_exe_wrapper(compiled, dstfn + clen + 1, db, i, "finish", 1) ;
        memcpy(dstfn + clen + 20 + len, ".user", 6) ;
      }
      if (!filecopy_unsafe(srcfn, dstfn, 0755))
      {
        cleanup(compiled) ;
        strerr_diefu4sys(111, "copy ", srcfn, " to ", dstfn) ;
      }
    }

    memcpy(srcfn + srcdirlen + len + 2, "timeout-kill", 13) ;
    memcpy(dstfn + clen + 14 + len, "timeout-kill", 13) ;
    filecopy_unsafe(srcfn, dstfn, 0644) ;

    memcpy(srcfn + srcdirlen + len + 2, "timeout-finish", 15) ;
    memcpy(dstfn + clen + 14 + len, "timeout-finish", 15) ;
    filecopy_unsafe(srcfn, dstfn, 0644) ;

    memcpy(srcfn + srcdirlen + len + 2, "nosetsid", 9) ;
    memcpy(dstfn + clen + 14 + len, "nosetsid", 9) ;
    filecopy_unsafe(srcfn, dstfn, 0644) ;

    memcpy(srcfn + srcdirlen + len + 2, "data", 5) ;
    memcpy(dstfn + clen + 14 + len, "data", 5) ;
    dircopy(compiled, srcfn, dstfn) ;

    memcpy(srcfn + srcdirlen + len + 2, "env", 4) ;
    memcpy(dstfn + clen + 14 + len, "env", 4) ;
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
    uint32_pack_big(pack + 32, sv->x.longrun.consumer) ;
    uint32_pack_big(pack + 36, sv->x.longrun.nproducers) ;
    uint32_pack_big(pack + 40, sv->x.longrun.producers) ;
    m = 44 ;
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
  size_t clen = strlen(compiled) ;
  buffer b ;
  int fd ;
  char buf[4096] ;
  char dbfn[clen + 4] ;
  memcpy(dbfn, compiled, clen) ;
  memcpy(dbfn + clen, "/db", 4) ;
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
  buffer_init(&b, &buffer_write, fd, buf, 4096) ;

  if (buffer_put(&b, S6RC_DB_BANNER_START, S6RC_DB_BANNER_START_LEN) < 0)
    goto err ;

  if (buffer_put(&b, db->string, db->stringlen) < 0) goto err ;

  {
    unsigned int i = db->ndeps << 1 ;
    uint32_t const *p = db->deps ;
    char pack[4] ;
    while (i--)
    {
      uint32_pack_big(pack, *p++) ;
      if (buffer_put(&b, pack, 4) < 0) goto err ;
    }
    i = db->nproducers ;
    p = db->producers ;
    while (i--)
    {
      uint32_pack_big(pack, *p++) ;
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
  uint32_t const *bdeps,
  uid_t const *uids,
  size_t uidn,
  gid_t const *gids,
  size_t gidn,
  char const *fdhuser,
  int blocking)
{
  if (verbosity >= 2) strerr_warni2x("writing compiled information to ", compiled) ;
  init_compiled(compiled) ;
  write_sizes(compiled, db) ;
  write_resolve(compiled, db, bundles, nbundles, bdeps) ;
  stralloc_free(&data) ;
  write_db(compiled, db) ;
  write_specials(compiled, db, uids, uidn, gids, gidn, fdhuser, blocking) ;
  write_servicedirs(compiled, db, srcdirs) ;
}

int main (int argc, char const *const *argv)
{
  before_t before = BEFORE_ZERO ;
  char const *compiled ;
  char const *fdhuser = 0 ;
  int blocking = 0 ;
  size_t uidn = 0, gidn = 0 ;
  uid_t uids[256] ;
  gid_t gids[256] ;
  PROG = "s6-rc-compile" ;
  {
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "v:u:g:h:b", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case 'u' : if (!uid_scanlist(uids, 255, l.arg, &uidn)) dieusage() ; break ;
        case 'g' : if (!gid_scanlist(gids, 255, l.arg, &gidn)) dieusage() ; break ;
        case 'h' : fdhuser = l.arg ; break ;
        case 'b' : blocking = 1 ; break ;
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
    uint32_t producers[before.nproducers ? before.nproducers : 1] ;
    s6rc_db_t db =
    {
      .services = servicesblob,
      .nshort = genalloc_len(oneshot_t, &before.oneshots),
      .nlong = genalloc_len(longrun_t, &before.longruns),
      .stringlen = keep.len,
      .nargvs = before.nargvs,
      .nproducers = before.nproducers,
      .producers = producers,
      .string = keep.s
    } ;
    char const *srcdirs[db.nlong] ;
    unsigned char sarray[nbits * n] ;
    bundle_t bundles[nbundles] ;
    unsigned char barray[nbits * nbundles] ;
    unsigned int nbdeps = resolve_bundles(genalloc_s(bundle_t, &before.bundles), bundles, db.nshort, db.nlong, nbundles, genalloc_s(unsigned int, &before.indices), barray) ;
    uint32_t bdeps[nbdeps] ;

    genalloc_free(bundle_t, &before.bundles) ;
    flatlist_bundles(bundles, nbundles, nbits, barray, bdeps) ;
    resolve_services(&db, &before, srcdirs, sarray, barray) ;
    genalloc_free(unsigned int, &before.indices) ;
    genalloc_free(oneshot_t, &before.oneshots) ;
    genalloc_free(longrun_t, &before.longruns) ;
    genalloc_free(nameinfo_t, &nameinfo) ;
    avltree_free(&names_map) ;
    {
      uint32_t deps[db.ndeps << 1] ;
      db.deps = deps ;
      flatlist_services(&db, sarray) ;
      write_compiled(compiled, &db, srcdirs, bundles, nbundles, bdeps, uids, uidn, gids, gidn, fdhuser, blocking) ;
    }
  }

  return 0 ;
}
