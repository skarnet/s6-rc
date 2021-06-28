/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <skalibs/alloc.h>
#include <skalibs/genalloc.h>
#include <skalibs/buffer.h>
#include <skalibs/djbunix.h>

#include <s6-rc/db.h>
#include "state.h"

void state_free (state_t *st, uint32_t const *dbn)
{
  for (s6rc_stype_t type = S6RC_STYPE_LONGRUN ; type < S6RC_STYPE_PHAIL ; type++)
  {
    alloc_free(st->sta[type]) ;
    for (size_t i = 0 ; i < dbn[S6RC_STYPE_PHAIL + type] ; i++)
      genalloc_free(instance_t, st->dyn[type] + i) ;
    alloc_free(st->dyn[type]) ;
  }
}

int state_ready (state_t *st, uint32_t const *dbn)
{
  s6rc_stype_t type = 0 ;
  for (; type < S6RC_STYPE_PHAIL ; type++)
  {
    genalloc *q ;
    stateatom_t *p = alloc(sizeof(stateatom_t) * dbn[type]) ;
    if (!p) goto err ;
    q = alloc(sizeof(genalloc) * dbn[S6RC_STYPE_PHAIL + type]) ;
    if (!q) { alloc_free(p) ; goto err ; }
    st->sta[type] = p ;
    for (size_t i = 0 ; i < dbn[S6RC_STYPE_PHAIL + type] ; i++) q[i] = genalloc_zero ;
    st->dyn[type] = q ;
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
  {
    char pack[4] ;
    uint32_pack_big(pack, dbn[type]) ;
    if (buffer_put(&b, pack, 4) < 4) goto err ;
  }
  for (s6rc_stype_t type = 0 ; type < S6RC_STYPE_PHAIL ; type++)
  {
    for (uint32_t i = 0 ; i < dbn[type] ; i++)
      if (!atom_write(&b, st->sta[type] + i)) goto err ;
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
