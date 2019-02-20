/* ISC license. */

#include <string.h>
#include <stdint.h>

#include <skalibs/posixishard.h>
#include <skalibs/buffer.h>
#include <skalibs/env.h>
#include <skalibs/djbunix.h>
#include <skalibs/unix-transactional.h>
#include <s6-rc/s6rc-db.h>

#ifdef DEBUG
#include <skalibs/lolstdio.h>
#define DBG(...) LOLDEBUG(__VA_ARGS__)
#else
#define DBG(...)
#endif

static int s6rc_db_check_valid_string (char const *string, size_t stringlen, size_t pos)
{
  if (pos >= stringlen) return 0 ;
  if (strnlen(string + pos, stringlen - pos) == stringlen - pos) return 0 ;
  return 1 ;
}

static inline int s6rc_db_check_valid_strings (char const *string, size_t stringlen, size_t pos, unsigned int n)
{
  while (n--)
  {
    if (!s6rc_db_check_valid_string(string, stringlen, pos)) return 0 ;
    pos += strlen(string + pos) + 1 ;
  }
  return 1 ;
}

static inline int s6rc_db_read_uints (buffer *b, unsigned int max, uint32_t *p, unsigned int n)
{
  uint32_t x ;
  while (n--)
  {
    if (!s6rc_db_read_uint32(b, &x)) return -1 ;
    if (x >= max) return 0 ;
    *p++ = x ;
  }
  return 1 ;
}


static inline int s6rc_db_read_services (buffer *b, s6rc_db_t *db)
{
  unsigned int n = db->nshort + db->nlong ;
  unsigned int nargvs = db->nargvs ;
  unsigned int argvpos = 0 ;
  unsigned int i = 0 ;
  for (; i < n ; i++)
  {
    s6rc_service_t *sv = db->services + i ;
    DBG("service %u/%u", i, n) ;
    if (!s6rc_db_read_uint32(b, &sv->name)) return -1 ;
    DBG("  name is %u: %s", sv->name, db->string + sv->name) ;
    if (sv->name >= db->stringlen) return 0 ;
    if (!s6rc_db_check_valid_string(db->string, db->stringlen, sv->name)) return 0 ;
    if (!s6rc_db_read_uint32(b, &sv->flags)) return -1 ;
    DBG("  flags is %x", sv->flags) ;
    if (!s6rc_db_read_uint32(b, &sv->timeout[0])) return -1 ;
    DBG("  timeout0 is %u", sv->timeout[0]) ;
    if (!s6rc_db_read_uint32(b, &sv->timeout[1])) return -1 ;
    DBG("  timeout1 is %u", sv->timeout[1]) ;
    if (!s6rc_db_read_uint32(b, &sv->ndeps[0])) return -1 ;
    DBG("  ndeps0 is %u", sv->ndeps[0]) ;
    if (!s6rc_db_read_uint32(b, &sv->ndeps[1])) return -1 ;
    DBG("  ndeps1 is %u", sv->ndeps[1]) ;
    if (!s6rc_db_read_uint32(b, &sv->deps[0])) return -1 ;
    DBG("  deps0 is %u", sv->deps[0]) ;
    if (sv->deps[0] > db->ndeps || sv->deps[0] + sv->ndeps[0] > db->ndeps)
      return 0 ;
    if (!s6rc_db_read_uint32(b, &sv->deps[1])) return -1 ;
    DBG("  deps1 is %u", sv->deps[1]) ;
    if (sv->deps[1] > db->ndeps || sv->deps[1] + sv->ndeps[1] > db->ndeps)
      return 0 ;
#ifdef DEBUG
    {
      unsigned int k = 0 ;
      for (; k < sv->ndeps[0] ; k++)
        DBG("   rev dep on %u", db->deps[sv->deps[0] + k]) ;
      for (k = 0 ; k < sv->ndeps[1] ; k++)
        DBG("   dep on %u", db->deps[db->ndeps + sv->deps[1] + k]) ;
    }
#endif
    if (i < db->nlong)
    {
      if (!s6rc_db_read_uint32(b, &sv->x.longrun.consumer)) return -1 ;
      if (!s6rc_db_read_uint32(b, &sv->x.longrun.nproducers)) return -1 ;
      if (!s6rc_db_read_uint32(b, &sv->x.longrun.producers)) return -1 ;
    }
    else
    {
      unsigned int j = 0 ;
      DBG("  oneshot") ;
      for (; j < 2 ; j++)
      {
        uint32_t pos, argc ;
        if (!s6rc_db_read_uint32(b, &argc)) return -1 ;
        DBG("    argc[%u] is %u, nargvs is %u", j, argc, nargvs) ;
        if (argc > nargvs) return 0 ;
        if (!s6rc_db_read_uint32(b, &pos)) return -1 ;
        DBG("    pos[%u] is %u", j, pos) ;
        if (!s6rc_db_check_valid_strings(db->string, db->stringlen, pos, argc)) return 0 ;
        if (!env_make((char const **)db->argvs + argvpos, argc, db->string + pos, db->stringlen - pos)) return -1 ;
        DBG("    first arg is %s", db->argvs[argvpos]) ;
        sv->x.oneshot.argv[j] = argvpos ;
        sv->x.oneshot.argc[j] = argc ;
        argvpos += argc ; nargvs -= argc ;
        if (!nargvs--) return 0 ;
        db->argvs[argvpos++] = 0 ;
      }
    }
    {
      char c ;
      if (buffer_get(b, &c, 1) < 1) return -1 ;
      if (c != '\376') return 0 ;
    }
  }
  if (nargvs) return 0 ;
  return 1 ;
}

static inline int s6rc_db_read_string (buffer *b, char *string, size_t len)
{
  if (buffer_get(b, string, len) < (ssize_t)len) return -1 ;
  return 1 ;
}

static inline int s6rc_db_read_buffer (buffer *b, s6rc_db_t *db)
{
  {
    char banner[S6RC_DB_BANNER_START_LEN] ;
    if (buffer_get(b, banner, S6RC_DB_BANNER_START_LEN) < S6RC_DB_BANNER_START_LEN) return -1 ;
    if (memcmp(banner, S6RC_DB_BANNER_START, S6RC_DB_BANNER_START_LEN)) return 0 ;
  }

  {
    int r = s6rc_db_read_string(b, db->string, db->stringlen) ;
    if (r < 1) return r ;
    r = s6rc_db_read_uints(b, db->nshort + db->nlong, db->deps, db->ndeps << 1) ;
    if (r < 1) return r ;
    r = s6rc_db_read_uints(b, db->nlong, db->producers, db->nproducers) ;
    if (r < 1) return r ;
    r = s6rc_db_read_services(b, db) ;
    if (r < 1) return r ;
  }

  {
    char banner[S6RC_DB_BANNER_END_LEN] ;
    if (buffer_get(b, banner, S6RC_DB_BANNER_END_LEN) < S6RC_DB_BANNER_END_LEN) return -1 ;
    if (memcmp(banner, S6RC_DB_BANNER_END, S6RC_DB_BANNER_END_LEN)) return 0 ;
  }
  return 1 ;
}

int s6rc_db_read (int fdcompiled, s6rc_db_t *db)
{
  int r ;
  buffer b ;
  char buf[BUFFER_INSIZE] ;
  int fd = open_readatb(fdcompiled, "db") ;
  if (fd < 0) return -1 ;
  buffer_init(&b, &buffer_read, fd, buf, BUFFER_INSIZE) ;
  r = s6rc_db_read_buffer(&b, db) ;
  fd_close(fd) ;
  return r ;
}
