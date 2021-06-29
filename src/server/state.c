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

static stateatom_t const stateatom_zero = STATEATOM_ZERO ;

void instance_free (instance_t *ins)
{
  dynstorage_remove(ins->param) ;
}

int instance_new (instance_t *ins, stateatom_t const *state, char const *param) ;
{
  char const *s = dynstorage_add(param) ;
  if (!s) return 0 ;
  ins->state = state ;
  inst->param = s ;
  return 1 ;
}

void state_free (state_t *st, uint32_t const *dbn)
{
  for (s6rc_stype_t type = S6RC_STYPE_LONGRUN ; type < S6RC_STYPE_PHAIL ; type++)
  {
    alloc_free(st->sta[type]) ;
    for (size_t i = 0 ; i < dbn[S6RC_STYPE_PHAIL + type] ; i++)
    {
      for (size_t j = 0 ; j < genalloc_len(instance_t, st->dyn[type] + i) ; j++)
        instance_free(genalloc_s(instance_t, st->dyn[type] + i) + j) ;
      genalloc_free(instance_t, st->dyn[type] + i) ;
    }
    alloc_free(st->dyn[type]) ;
  }
}

int state_init (state_t *st, uint32_t const *dbn)
{
  s6rc_stype_t type = 0 ;
  for (; type < S6RC_STYPE_PHAIL ; type++)
  {
    st->sta[type] = alloc(sizeof(stateatom_t) * dbn[type]) ;
    if (!st->sta[type]) goto err ;
    st->dyn[type] = alloc(sizeof(genalloc) * dbn[S6RC_STYPE_PHAIL + type]) ;
    if (!st->dyn[type]) { alloc_free(st->sta[type]) ; goto err ; }
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      st->sta[type][i] = stateatom_zero ;
    for (size_t i = 0 ; i < dbn[S6RC_STYPE_PHAIL + type] ; i++)
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

static int atom_write (buffer *b, stateatom_t const *state)
{
  char c = state->wanted | (state->current << 1) | (state->transitioning << 2) ;
  return buffer_put(b, &c, 1) == 1 ;
}

int state_write (char const *file, state_t const *st, uint32_t const *dbn)
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

  for (s6rc_stype_t type = 0 ; type < S6RC_STYPE_PHAIL ; type++)
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      if (!atom_write(&b, st->sta[type] + i)) goto err ;

  for (s6rc_stype_t type = 0 ; type < S6RC_STYPE_PHAIL ; type++)
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_PHAIL + type] ; i++)
    {
      uint32_t n = genalloc_len(instance_t, st->dyn[type] + i) ;
      instance_t const *p = genalloc_s(instance_t, st->dyn[type] + i) ;
      char pack[4] ;
      uint32_pack_big(pack, n) ;
      if (buffer_put(&b, pack, 4) < 4) goto err ;
      for (uint32_t j = 0 ; j < n ; j++)
      {
        uint32_t len = strlen(p[j].param) ;
        uint32_pack_big(pack, len) ;
        if (!atom_write(&p[j].state)) goto err ;
        if (buffer_put(&b, pack, 4) < 4) goto err ;
        if (buffer_put(&b, p[j].param, len+1) < len+1) goto err ;
      }
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

static int atom_read (buffer *b, stateatom_t *state)
{
  char c ;
  if (buffer_get(b, &c, 1) < 1) return 0 ;
  state->wanted = c & 1 ;
  state->current = !!(c & 2) ;
  state->transitioning = !!(c & 4) ;
  return 1 ;
}

int state_read (char const *file, state_t *st, uint32_t const *dbn)
{
  int fd ;
  buffer b ;
  char buf[1024] ;
  if (!state_init(st, dbn)) return 0 ;
  fd = openc_read(file) ;
  if (fd == -1) goto err0 ;
  buffer_init(&b, &buffer_read, fd, buf, 1024) ;

  for (s6rc_stype_t type = 0 ; type < S6RC_STYPE_PHAIL ; type++)
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      if (!atom_read(&b, st->sta[type] + i)) goto err ;

  for (s6rc_stype_t type = 0 ; type < S6RC_STYPE_PHAIL ; type++)
    for (uint32_t i = 0 ; i < dbn[S6RC_STYPE_PHAIL + type] ; i++)
    {
      uint32_t n ;
      char pack[4] ;
      if (buffer_get(&b, pack, 4) < 4) goto err ;
      uint32_unpack_big(pack, &n) ;
      if (n > S6RC_INSTANCES_MAX) goto eproto ;
      if (!genalloc_ready(instance_t, st->dyn[type] + i, n)) goto err ;
      for (uint32_t j = 0 ; i < n ; j++)
      {
        uint32_t len ;
        instance_t *ins = genalloc_s(instance_t, st->dyn[type] + i) + j ;
        if (!atom_read(&b, &ins->state)) goto err ;
        if (buffer_get(&b, pack, 4) < 4) goto err ;
        uint32_unpack_big(pack, &len) ;
        if (len > S6RC_INSTANCE_MAXLEN) goto eproto ;
        {
          char param[len + 1] ;
          if (buffer_get(&b, param, len + 1) < len + 1) goto err ;
          if (param[len]) goto eproto ;
          ins->param = dynstorage_add(param) ;
          if (!ins_param) goto err ;
        }
      }
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
  state_free(st, dbn) ;
  return 0 ;
}

stateatom_t *state_atom (state_t const *st, s6rc_sid_t const *id)
{
  if (id->param)
  {
    size_t n = genalloc_len(instance_t, st->dyn[id->stype] + id->i) ;
    instance_t *instances = genalloc_s(instance_t, st->dyn[id->stype] + id->i) ;
    for (size_t i = 0 ; i < n ; i++)
      if (!strcmp(id->param, instances[i].param)) return &instances[i].state ;
    return 0 ;
  }
  else return st->sta[id->stype] + id->i ;
}
