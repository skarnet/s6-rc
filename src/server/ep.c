/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <skalibs/strerr2.h>
#include <skalibs/genalloc.h>
#include <skalibs/gensetdyn.h>
#include <skalibs/avltree.h>

#include "db.h"
#include "event.h"
#include "ep.h"

typedef struct epelem_s epelem_t, *epelem_t_ref ;
struct epelem_s
{
  s6rc_id_t owner ;
  ep_func_t_ref f ;
  void *aux ;
} ;
#define EPELEM_ZERO { .owner = 0, .f = 0, .aux = 0 }

typedef struct eplist_s eplist_t, *eplist_t_ref ;
struct eplist_s
{
  char const *name ;
  genalloc list ; /* epelem_t */
} ;
#define EPLIST_ZERO { .name = 0, .list = GENALLOC_ZERO }

typedef struct epset_s epset_t, *epset_t_ref ;
struct epset_s
{
  gensetdyn data ; /* eplist_t */
  avltree map ;
} ;
#define EPSET_ZERO { .data = GENSETDYN_ZERO, .map = AVLTREE_ZERO }

static void *epset_dtok (uint32_t d, void *x)
{
  return (void *)&GENSETDYN_P(eplist_t, (gensetdyn *)x, d)->name ;
}

static int epset_cmp (void const *a, void const *b, void *x)
{
  (void)x ;
  return strcmp((char const *)a, (char const *)b) ;
}

static epset_t ep[EVENTTYPE_PHAIL] =
{
  { .data = GENSETDYN_ZERO, .map = AVLTREE_INIT(8, 3, 8, &epset_dtok, &epset_cmp, &ep[0].data) },
  { .data = GENSETDYN_ZERO, .map = AVLTREE_INIT(8, 3, 8, &epset_dtok, &epset_cmp, &ep[1].data) },
  { .data = GENSETDYN_ZERO, .map = AVLTREE_INIT(8, 3, 8, &epset_dtok, &epset_cmp, &ep[2].data) },
  { .data = GENSETDYN_ZERO, .map = AVLTREE_INIT(8, 3, 8, &epset_dtok, &epset_cmp, &ep[3].data) },
  { .data = GENSETDYN_ZERO, .map = AVLTREE_INIT(8, 3, 8, &epset_dtok, &epset_cmp, &ep[4].data) },
  { .data = GENSETDYN_ZERO, .map = AVLTREE_INIT(8, 3, 8, &epset_dtok, &epset_cmp, &ep[5].data) },
  { .data = GENSETDYN_ZERO, .map = AVLTREE_INIT(8, 3, 8, &epset_dtok, &epset_cmp, &ep[6].data) },
} ;

static void eplist_free (void *p)
{
  eplist_t *x = p ;
  genalloc_free(epelem_t, &x->list) ;
}

void ep_free ()
{
  for (s6rc_eventtype_t i = 0 ; i < S6RC_EVENTTYPE_PHAIL ; i++)
  {
    avltree_free(&ep[i].map) ;
    gensetdyn_deepfree(&ep[i].data, &eplist_free) ;
    avltree_init(&ep[i].map, 8, 3, 8, &epset_dtok, &epset_cmp, &ep[i].data) ;
  }
}

int ep_add (uint8_t type, char const *name, uint32_t owner, ep_func_t_ref f, void *aux)
{
  epelem_t ee = { .owner = owner, .f = f, .aux = aux } ;
  uint32_t d ;
  if (!avltree_search(&ep[type].map, name, &d))
  {
    eplist_t newlist = { .name = name, .list = GENALLOC_ZERO } ;
    if (!gensetdyn_new(&ep[type].data, &d)) return 0 ;
    *GENSETDYN_P(eplist_t, &ep[type].data, d) = newlist ;
    if (!avltree_insert(&ep[type].map, d))
    {
      gensetdyn_delete(&ep[type].data, d) ;
      return 0 ;
    }
  }
  return genalloc_catb(epelem_t, &GENSETDYN_P(eplist_t, &ep[type].data, d)->list, &ee) ;
}

void ep_delete (uint8_t type, char const *name, uint32_t owner, ep_func_t_ref f, void *aux)
{
  uint32_t d ;
  if (!avltree_search(&ep[type].map, name, &d)) return ;

  epelem_t ee = { .owner = owner, .f = f, .aux = aux } ;
  genalloc *g = &GENSETDYN_P(eplist_t, &ep[type].data, d)->list ;
  epelem_t *list = genalloc_s(epelem_t, g) ;
  size_t n = genalloc_len(epelem_t, g) ;
  size_t i = 0 ;
  for (; i < n ; i++) if (list == ee) break ;
  if (i < n) list[i] = list[--n] ;
  if (!n)
  {
    genalloc_free(epelem_t, g) ;
    avltree_delete(&ep[type].map, name) ;
    gensetdyn_delete(eplist_t, &ep[type].data, d) ;
  }
}

void ep_run (s6rc_event_t const *ev)
{
  uint32_t d ;
  if (!avltree_search(&ep[ev->type].map, ev->name, &d)) return ;

  genalloc *g = &GENSETDYN_P(eplist_t, &ep[ev->type].data, d)->list ;
  epelem_t const *list = genalloc_s(epelem_t, g) ;
  size_t n = genalloc_len(epelem_t, g) ;
  for (size_t i = 0 ; i < n ; i++)
    (*list[i].f)(ev, list[i].owner, list[i].aux) ;
}
