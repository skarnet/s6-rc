/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <skalibs/uint32.h>
#include <skalibs/alloc.h>
#include <skalibs/buffer.h>
#include <skalibs/djbunix.h>
#include <skalibs/cdb.h>
#include <skalibs/sha256.h>

#include <s6-rc/db.h>

#include <skalibs/posixishard.h>

static int gethu32 (buffer *b, SHA256Schedule *ctx, uint32_t *n)
{
  if (buffer_get(b, (char *)n, 4) < 4) return 0 ;
  sha256_update(ctx, (char *)n, 4) ;
  return 1 ;
}

int s6rc_db_init (s6rc_db_t *db, char const *dir)
{
  SHA256Schedule ctx = SHA256_INIT() ;
  uint32_t ntotal, ndeps, nproducers, storagelen, nargv ;
  size_t len = strlen(dir) ;
  buffer b ;
  int fd ;
  int dfd = openc_read(dir) ;
  char buf[4096] ;
  if (dfd == -1) return 0 ;
  fd = openc_readat(dfd, "db_nomap") ;
  if (fd == -1) goto errd ;
  buffer_init(&b, &buffer_read, fd, buf, 4096) ;
  {
    uint32_t canary ;
    if (!gethu32(&b, &canary)) goto err0 ;
    if (canary != 0x11223344u) { errno = EILSEQ ; goto err0 ; }
  }
  if (!gethu32(&b, &ctx, &ntotal)) goto err0 ;
  if (!gethu32(&b, &ctx, &ndeps)) goto err0 ;
  if (!gethu32(&b, &ctx, &nproducers)) goto err0 ;
  if (!gethu32(&b, &ctx, &storagelen)) goto err0 ;
  if (!gethu32(&b, &ctx, &nargv)) goto err0 ;
  if (nargv > S6RC_ARGV_MAX) goto eproto0 ;

  {
    uint32_t argvs[nargv ? nargv : 1] ;
    if (buffer_get(&b, (char *)argvs, nargv * 4) < nargv * 4) goto err0 ;
    {
      char c ;
      ssize_t r = buffer_get(&b, &c, 1) ;
      if (r < 0) goto err0 ;
      if (r) goto eproto0 ;
    }
    fd_close(fd) ;
    sha256_update(&ctx, (char *)argvs, nargv * 4) ;

    {
      struct stat st ;
      void *map ;
      fd = openc_readat(dfd, "db") ;
      if (fd == -1) goto errd ;
      if (fstat(fd, &st) == -1) goto err0 ;
      if (!S_ISREG(st.st_mode) || st.st_size < 8 * S6RC_STYPE_N) goto eproto0 ;
      map = mmap(0, st.st_size, PROT_READ, MAP_SHARED, fd, 0) ;
      if (map == MAP_FAILED) goto err0 ;
      fd_close(fd) ;
      db->size = st.st_size ;
      db->map = map ;
    }
    db->n = (uint32_t const *)db->map ;
    if (ntotal != db->n[0] + db->n[1] + db->n[2] + db->n[3] + db->n[4] + db->n[5] + db->n[6] + db->n[7] + db->n[8] + db->n[9])
    {
      errno = EPROTO ;
      goto err1 ;
    }
    if (!cdb_init_at(&db->resolve, dfd, "resolve.cdb")) goto err1 ;
    {
      ssize_t r = openreadnclose_at(dfd, "hash", buf+32, 33) ;
      if (r == 33) goto eproto2 ;
      if (r < 32) goto err2 ;
      sha256_update(&ctx, db->map, db->size) ;
      sha256_update(&ctx, db->resolve.map, db->resolve.size) ;
      sha256_final(&ctx, buf) ;
      if (memcmp(buf, buf+32, 32)) goto eproto2 ;
    }

    db->longruns = (s6rc_longrun_t const *)(db->map + 4 * 2 * S6RC_STYPE_N) ;
    db->oneshots = (s6rc_oneshot_t const *)(db->longruns + db->n[S6RC_STYPE_LONGRUN] + db->n[S6RC_STYPE_N + S6RC_STYPE_LONGRUN]) ;
    db->externals = (s6rc_external_t const *)(db->oneshots + db->n[S6RC_STYPE_ONESHOT] + db->n[S6RC_STYPE_N + S6RC_STYPE_ONESHOT]) ;
    db->bundles = (s6rc_bundle_t const *)(db->externals + db->n[S6RC_STYPE_EXTERNAL] + db->n[S6RC_STYPE_N + S6RC_STYPE_EXTERNAL]) ;
    db->virtuals = (s6rc_bundle_t const *)(db->bundles + db->n[S6RC_STYPE_BUNDLE] + db->n[S6RC_STYPE_N + S6RC_STYPE_BUNDLE]) ;
    db->deps[0] = (uint32_t const *)(db->virtuals + db->n[S6RC_STYPE_VIRTUAL] + db->n[S6RC_STYPE_N + S6RC_STYPE_VIRTUAL]) ;
    db->deps[1] = db->deps[0] + ndeps ;
    db->producers = db->deps[1] + ndeps ;
    db->deptypes[0] = (uint8_t const *)(db->producers + nproducers) ;
    db->deptypes[1] = db->deptypes[0] + ndeps ;
    db->storage = (char const *)(db->deptypes[1] + ndeps) ;
    if (db->storage + storagelen != db->map + db->size) goto eproto2 ;

    db->argvs = (char const **)alloc(sizeof(char const *) * nargv) ;
    if (!db->argvs) goto err2 ;
    for (uint32_t i = 0 ; i < nargv ; i++)
      db->argvs[i] = argvs[i] ? db->storage + argvs[i] : 0 ;
  }
  fd_close(dfd) ;
  return 1 ;

 eproto2:
  errno = EPROTO ;
 err2:
  cdb_free(&db->resolve) ;
 err1:
  munmap_void(db->map, db->size) ;
  db->map = 0 ;
  goto errd ;
 eproto0:
   errno = EPROTO ;
 err0:
  fd_close(fd) ;
 errd:
  fd_close(dfd) ;
  return 0 ;
}
