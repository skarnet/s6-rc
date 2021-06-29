/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <skalibs/alloc.h>
#include <skalibs/buffer.h>
#include <skalibs/djbunix.h>
#include <skalibs/sha256.h>

#include "db.h"

#include <skalibs/posixishard.h>

static int geth (buffer *b, SHA256Schedule *ctx, char *s, size_t len)
{
  if (buffer_get(b, s, len) < len) return 0 ;
  sha256_update(ctx, s, len) ;
  return 1 ;
}

static int readu32 (buffer *b, SHA256Schedule *ctx, uint32_t *x)
{
  char pack[4] ;
  if (!geth(b, ctx, pack, 4)) return 0 ;
  uint32_unpack_big(pack, x) ;
  return 1 ;
}

static int readdeps (buffer *b, SHA256Schedule *ctx, s6rc_baseid_t *deps, uint32_t ndeps, uint32_t const *dbn)
{
  for (uint32_t i = 0 ; i < (ndeps << 1) ; i++)
  {
    char pack[5] ;
    if (!geth(b, ctx, pack, 5)) return 0 ;
    if ((unsigned char)pack[0] >= S6RC_STYPE_PHAIL) return (errno = EPROTO, 0) ;
    deps[i].stype = pack[0] ;
    uint32_unpack_big(pack + 1, &deps[i].i) ;
  }
  return 1 ;
}

int s6rc_db_read_main (char const *dir, s6rc_db_t const *db, char *dbhash)
{
  SHA256Schedule ctx = SHA256_INIT() ;
  int fd ;
  buffer b ;
  size_t len = strlen(dir) ;
  uint32_t ndeps, nproducers, nargvs, storagelen ;
  char buf[1024] ;
  char fn[len + 4] ;
  memcpy(fn, dir, len) ;
  memcpy(fn + len, "/db", 4) ;
  fd = openc_read(fn) ;
  if (fd == -1) return 0 ;
  buffer_init(&b, &buffer_read, fd, buf, 1024) ;

  {
    char banner[S6RC_DB_BANNER_START_LEN] ;
    if (!geth(&b, &ctx, banner, S6RC_DB_BANNER_START_LEN)) goto err0 ;
    if (memcmp(banner, S6RC_DB_BANNER_START, S6RC_DB_BANNER_START_LEN)) { errno = EPROTO ; goto err0 ; }
  }

  {
    uint32_t ntotal ;
    uint32_t nacc = 0 ;
    if (!readu32(&b, &ctx, &ntotal)) goto err0 ;
    for (s6rc_stype_t type = 0 ; (type < S6RC_STYPE_PHAIL << 1) ; type++)
    {
      if (!readu32(&b, &ctx, db->n + type)) goto err0 ;
      nacc += db->n[type] ;
    }
    if (ntotal != nacc) { errno = EPROTO ; goto err0 ; }
  }
  if (!readu32(&b, &ctx, &ndeps)) goto err0 ;
  if (!readu32(&b, &ctx, &nproducers)) goto err0 ;
  if (!readu32(&b, &ctx, &nargvs)) goto err0 ;
  if (!readu32(&b, &ctx, &storagelen)) goto err0 ;

  db->longruns = alloc(sizeof(s6rc_longrun_t) * (db->n[S6RC_STYPE_LONGRUN] + db->n[S6RC_STYPE_PHAIL + S6RC_STYPE_LONGRUN])) ;
  if (!db->longruns) goto err0 ;
  db->oneshots = alloc(sizeof(s6rc_oneshot_t) * (db->n[S6RC_STYPE_ONESHOT] + db->n[S6RC_STYPE_PHAIL + S6RC_STYPE_ONESHOT])) ;
  if (!db->oneshots) goto err1 ;
  db->externals = alloc(sizeof(s6rc_external_t) * (db->n[S6RC_STYPE_EXTERNAL] + db->n[S6RC_STYPE_PHAIL + S6RC_STYPE_EXTERNAL])) ;
  if (!db->externals) goto err2 ;
  db->bundles = alloc(sizeof(s6rc_bundle_t) * (db->n[S6RC_STYPE_BUNDLE] + db->n[S6RC_STYPE_PHAIL + S6RC_STYPE_BUNDLE] + db->n[S6RC_STYPE_VIRTUAL] + db->n[S6RC_STYPE_PHAIL + S6RC_TYPE_VIRTUAL])) ;
  if (!db->bundles) goto err3 ;
  db->virtuals = db->bundles + db->n[S6RC_STYPE_BUNDLE] + db->n[S6RC_STYPE_PHAIL + S6RC_STYPE_BUNDLE])) ;
  db->deps[0] = alloc(sizeof(s6rc_baseid_t) * (ndeps << 1)) ;
  if (!db->deps[0]) goto err4 ;
  db->deps[1] = db->deps[0] + ndeps ;
  db->deptypes[0] = alloc(sizeof(s6rc_deptype_t) * (ndeps << 1)) ;
  if (!db->deptypes[0]) goto err5 ;
  db->deptypes[1] = db->deptypes[0] + ndeps ;
  db->producers = alloc(sizeof(s6rc_baseid_t) * nproducers) ;
  if (!db->producers) goto err6 ;
  db->argvs = alloc(sizeof(char const *) * nargvs) ;
  if (!db->longruns) goto err7 ;
  db->storage = alloc(storagelen) ;
  if (!db->storage) goto err8 ;

  if (!geth(&b, &ctx, db->storage, storagelen)) goto err ;
  if (!geth(&b, &ctx, db->deptypes[0], sizeof(s6rc_deptype_t) * (ndeps << 1))) goto err ;
  if (!readdeps(&b, &ctx, db->deps[0], ndeps, db->n)) goto err ;

  sha256_final(&ctx, dbhash) ;
  return 1 ;

 err:
  alloc_free(db->storage) ;
 err8:
  alloc_free(db->argvs) ;
 err7:
  alloc_free(db->producers) ;
 err6:
  alloc_free(db->deptypes[0]) ;
 err5:
  alloc_free(db->deps[0]) ;
 err4:
  alloc_free(db->bundles) ;
 err3:
  alloc_free(db->externals) ;
 err2:
  alloc_free(db->oneshots) ;
 err1:
  alloc_free(db->longruns) ;
 err0:
  fd_close(fd) ;
  return 0 ;
}
