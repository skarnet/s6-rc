/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <skalibs/alloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/buffer.h>
#include <skalibs/djbunix.h>

#include "db.h"
#include "dynstorage.h"
#include "state.h"

#include <skalibs/posixishard.h>

static sstate_t const sstate_zero = SSTATE_ZERO ;

void instance_free (instance_t *ins)
{
  dynstorage_remove(ins->param) ;
}

int instance_new (instance_t *ins, sstate_t const *st, char const *param) ;
{
  char const *s = dynstorage_add(param) ;
  if (!s) return 0 ;
  ins->state = *st ;
  inst->param = s ;
  return 1 ;
}

void mstate_free (mstate_t *st, uint32_t const *dbn)
{
  for (size_t type = 0 ; type < S6RC_STYPE_N ; type++)
  {
    alloc_free(st->sta[type]) ;
    for (size_t i = 0 ; i < dbn[S6RC_STYPE_N + type] ; i++)
    {
      for (size_t j = 0 ; j < genalloc_len(instance_t, st->dyn[type] + i) ; j++)
        instance_free(genalloc_s(instance_t, st->dyn[type] + i) + j) ;
      genalloc_free(instance_t, st->dyn[type] + i) ;
    }
    alloc_free(st->dyn[type]) ;
  }
}

int mstate_init (mstate_t *st, uint32_t const *dbn)
{
  size_t type = 0 ;
  for (; type < S6RC_STYPE_N ; type++)
  {
    st->sta[type] = alloc(dbn[type]) ;
    if (!st->sta[type]) goto err ;
    st->dyn[type] = alloc(sizeof(genalloc) * dbn[S6RC_STYPE_N + type]) ;
    if (!st->dyn[type]) { alloc_free(st->sta[type]) ; goto err ; }
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      st->sta[type][i] = sstate_zero ;
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_N + type] ; i++)
      st->dyn[type][i] = genalloc_zero ;
  }
  return 1 ;

 err:
  while (type--)
  {
    alloc_free(st->dyn[type]) ;
    alloc_free(st->sta[type]) ;
  }
  return 0 ;
}

static int sstate_write (buffer *b, sstate_t const *state)
{
  return buffer_put(b, (char *)state->bits, 1) == 1 ;
}

static int sstate_read (buffer *b, sstate_t *state)
{
  return buffer_get(b, (char *)state->bits, 1) == 1 ;
}

static inline int instance_write (buffer *b, instance_t const *ins)
{
  uint32_t len = strlen(ins->param) ;
  char pack[4] ;
  uint32_pack_big(pack, len) ;
  if (!sstate_write(b, &ins->state)) return 0 ;
  if (buffer_put(b, pack, 4) < 4) return 0 ;
  if (buffer_put(b, ins->param, len+1) < len+1) return 0 ;
  return 1 ;
}

static inline int instance_read (buffer *b, instance_t *ins)
{
  sstate_t st ;
  char const *p ;
  uint32_t len ;
  char pack[4] ;
  if (!sstate_read(b, &st)) return 0 ;
  if (buffer_get(&b, pack, 4) < 4) return 0 ;
  uint32_unpack_big(pack, &len) ;
  if (len > S6RC_INSTANCE_MAXLEN) return (errno = EPROTO, 0) ;
  {
    char param[len + 1] ;
    if (buffer_get(&b, param, len + 1) < len + 1) return 0 ;
    if (param[len]) return (errno = EPROTO, 0) ;
    p = dynstorage_add(param) ;
    if (!p) return 0 ;
  }
  ins->sstate = st ;
  ins->param = p ;
  return 1 ;
}

int mstate_write (char const *file, mstate_t const *st, uint32_t const *dbn)
{
  size_t filelen = strlen(file) ;
  int fd ;
  buffer b ;
  char buf[1024] ;
  char fn[filelen + 12] ;
  memcpy(fn, file, filelen) ;
  memcpy(fn + filelen, ":tmp:XXXXXX", 12) ;
  fd = mkstemp(fn) ;
  if (fd == -1) return 0 ;
  buffer_init(&b, &buffer_write, fd, buf, 1024) ;

  for (size_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      if (!sstate_write(&b, st->sta[type] + i)) goto err ;

  for (size_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_N + type] ; i++)
    {
      uint32_t n = genalloc_len(instance_t, st->dyn[type] + i) ;
      instance_t const *p = genalloc_s(instance_t, st->dyn[type] + i) ;
      char pack[4] ;
      uint32_pack_big(pack, n) ;
      if (buffer_put(&b, pack, 4) < 4) goto err ;
      for (uint32_t j = 0 ; j < n ; j++)
        if (!instance_write(&b, p + j)) goto err ;
    }

  if (!buffer_flush(&b)) goto err ;
  fd_close(fd) ;
  if (rename(fn, file) == -1) goto err0 ;
  return 1 ;

 err:
  fd_close(fd) ;
 err0:
  unlink_void(fn) ;
  return 0 ;
}

int mstate_read (char const *file, mstate_t *st, uint32_t const *dbn)
{
  int fd ;
  buffer b ;
  char buf[1024] ;
  if (!mstate_init(st, dbn)) return 0 ;
  fd = openc_read(file) ;
  if (fd == -1) goto err0 ;
  buffer_init(&b, &buffer_read, fd, buf, 1024) ;

  for (size_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      if (!sstate_read(&b, st->sta[type] + i)) goto err ;

  for (size_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_N + type] ; i++)
    {
      uint32_t n ;
      char pack[4] ;
      if (buffer_get(&b, pack, 4) < 4) goto err ;
      uint32_unpack_big(pack, &n) ;
      if (n > S6RC_INSTANCES_MAX) goto eproto ;
      if (!genalloc_ready(instance_t, st->dyn[type] + i, n)) goto err ;
      for (uint32_t j = 0 ; j < n ; j++)
        if (!instance_read(&b, genalloc_s(instance_t, st->dyn[type] + i) + j)) goto err ;
      genalloc_setlen(instance_t, st->dyn[type] + i, n) ;
    }

  {
    char c ;
    switch (buffer_get(&b, &c, 1))
    {
      case -1 : goto err ;
      case 1 : goto eproto ;
    }
  }

  fd_close(fd) ;
  return 1 ;

 eproto:
  errno = EPROTO ;
 err:
  fd_close(fd) ;
 err0:
  mstate_free(st, dbn) ;
  return 0 ;
}

sstate_t *sstate (mstate_t const *m, s6rc_id_t id, char const *param)
{
  if (stype(id) >= S6RC_STYPE_N)
  {
    size_t n = genalloc_len(instance_t, st->dyn[stype(id)] + snum(id)) ;
    instance_t *instances = genalloc_s(instance_t, st->dyn[stype(id)] + snum(id)) ;
    for (size_t i = 0 ; i < n ; i++)
      if (!strcmp(param, instances[i].param)) return &instances[i].sstate ;
    return STATE_PHAIL ;
  }
  else return st->sta[stype(id)] + snum(id) ;
}

int state_deps_fulfilled (s6rc_db_t const *db, mstate_t const *m, s6rc_id_t id, char const *param, int h)
{
  s6rc_common_t const *common = s6rc_service_common(db, id) ;
  for (uint32_t i = 0 ; i < common->ndeps[h] ; i++)
  {
    uint8_t deptype = db->deptypes[h][common->deps[h] + i] ;
    sstate_t *st = sstate(m, db->deps[h][common->deps[h] + i], param) ;
  }
}
