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

#include <s6-rc/db.h>
#include "dynstorage.h"
#include "state.h"

#include <skalibs/posixishard.h>

static sstate_t const sstate_zero = SSTATE_ZERO ;

static inline void instance_free (instance_t *ins)
{
  dynstorage_remove(ins->param) ;
}

void instance_remove (mstate_t *m, uint8_t type, uint32_t num, char const *param)
{
  genalloc *g = m->dyn[type] + num ;
  instance_t *ins = genalloc_s(instance_t, g) ;
  uint32_t n = genalloc_len(instance_t, g) ;
  uint32_t i = 0 ;
  for (; i < n ; i++) if (!strcmp(param, ins[i].param)) break ;
  if (i >= n) return ;
  instance_free(ins + i) ;
  ins[i] = ins[n-1] ;
  genalloc_setlen(instance_t, g, n-1) ;
}

sstate_t *instance_create (mstate_t *m, uint8_t type, uint32_t num, char const *param)
{
  genalloc *g = m->dyn[type] + num ;
  instance_t ins = { .sstate = SSTATE_ZERO, .param = dynstorage_add(param) } ;
  if (!ins.param) return 0 ;
  if (!genalloc_catb(instance_t, g, &ins, 1))
  {
    dynstorage_remove(ins.param) ;
    return 0 ;
  }
  return &genalloc_s(instance_t, g)[genalloc_len(instance_t, g) - 1].sstate ;
}

void mstate_free (mstate_t *st, uint32_t const *dbn)
{
  for (uint8_t type = 0 ; type < S6RC_STYPE_N ; type++)
  {
    alloc_free(st->sta[type]) ;
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_N + type] ; i++)
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
  uint8_t type = 0 ;
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

int mstate_iterate (mstate_t *m, uint32_t const *dbn, sstate_func_ref staf, instancelen_func_ref lenf, instance_func_ref dynf, void *arg)
{
  for (uint8_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      if (!staf(m->sta[type] + i, arg)) return 0 ;

  for (uint8_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_N + type] ; i++)
    {
      uint32_t n = genalloc_len(instance_t, m->dyn[type] + i) ;
      instance_t const *p = genalloc_s(instance_t, m->dyn[type] + i) ;
      if (!lenf(n, arg)) return 0 ;
      for (size_t j = 0 ; j < n ; j++)
        if (!dynf(p + j, arg)) goto err ;
    }

  return 1 ;
}

static int sstate_write (sstate_t *state, void *b)
{
  return buffer_put((buffer *)b, (char *)&state->bits, 1) == 1 ;
}

static int instancelen_write (uint32_t n, void *b)
{
  char pack[4] ;
  uint32_pack_big(pack, n) ;
  return buffer_put((buffer *)b, pack, 4) == 4 ;
}

static int instance_write (instance_t *ins, void *arg)
{
  buffer *b = arg ;
  uint32_t len = strlen(ins->param) ;
  char pack[4] ;
  uint32_pack_big(pack, len) ;
  if (!sstate_write(b, &ins->state)) return 0 ;
  if (buffer_put(b, pack, 4) < 4) return 0 ;
  if (buffer_put(b, ins->param, len+1) < len+1) return 0 ;
  return 1 ;
}

int mstate_write (char const *file, mstate_t const *m, uint32_t const *dbn)
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
  if (!mstate_iterate(m, &sstate_write, &instancelen_write, &instance_write, &b)) goto err ;
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

static int sstate_read (buffer *b, sstate_t *state)
{
  return buffer_get(b, (char *)&state->bits, 1) == 1 ;
}

static int instance_read (buffer *b, instance_t *ins)
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

int mstate_read (char const *file, mstate_t *st, uint32_t const *dbn)
{
  int fd ;
  buffer b ;
  char buf[1024] ;
  if (!mstate_init(st, dbn)) return 0 ;
  fd = openc_read(file) ;
  if (fd == -1) goto err0 ;
  buffer_init(&b, &buffer_read, fd, buf, 1024) ;

  for (uint8_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      if (!sstate_read(&b, st->sta[type] + i)) goto err ;

  for (uint8_t type = 0 ; type < S6RC_STYPE_N ; type++)
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_N + type] ; i++)
    {
      uint32_t n ;
      char pack[4] ;
      if (buffer_get(&b, pack, 4) < 4) goto err ;
      uint32_unpack_big(pack, &n) ;
      if (n > S6RC_INSTANCES_MAX) goto eproto ;
      if (!genalloc_ready(instance_t, st->dyn[type] + i, n)) goto err ;
      for (size_t j = 0 ; j < n ; j++)
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

sstate_t *sstate_tn (mstate_t *m, uint8_t type, uint32_t num, char const *param)
{
  if (type >= S6RC_STYPE_N)
  {
    size_t n = genalloc_len(instance_t, m->dyn[type] + num) ;
    instance_t *instances = genalloc_s(instance_t, m->dyn[type] + num) ;
    for (size_t i = 0 ; i < n ; i++)
      if (!strcmp(param, instances[i].param)) return &instances[i].sstate ;
    return 0 ;
  }
  else return m->sta[type] + num ;
}

sstate_t *sstate (uint32_t const *dbn, mstate_t *m, uint32_t id, char const *param)
{
  uint32_t num ;
  uint8_t type ;
  s6rc_service_typenum(dbn, id, &type, &num) ;
  return sstate_tn(m, type, num, param) ;
}

static int instancelen_nop_ (uint32_t n, void *arg)
{
  (void)n ;
  (void)arg ;
  return 1 ;
}

instancelen_func_ref const instancelen_nop = &instancelen_nop_ ;
