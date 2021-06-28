/* ISC license. */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <skalibs/gensetdyn.h>
#include <skalibs/avltree.h>

#include "dynstorage.h"

/*
  This is probably way overengineered, we could strdup the instance parameters
  into the instances themselves; there would be a little param duplication, but
  likely not much, and chances are we would save memory.
  However, a fundamental principle of C memory management is: separate structure
  from storage. Failing to abide by it leads to spaghettification of storage
  paths, and ultimately memory leaks.
  So, we store all our dynamic strings (i.e. user-provided strings that only
  appear in transient storage like buffers but that need a longer lifetime) here,
  and nowhere else. Duplication is prevented via refcounting.
*/

typedef struct cell_s cell_t, *cell_t_ref ;
struct cell_s
{
  char *s ;
  uint32_t refcount ;
} ;
#define CELL_ZERO { .s = 0, .refcount = 0 } ;

static gensetdyn cells = GENSETDYN_INIT(cell_t, 3, 3, 8) ;

static void *dynstorage_dtok (uint32_t d, void *data)
{
  return GENSETDYN_P(cell_t, (gensetdyn *)data, d)->s ;
}

static int dynstorage_cmp (void const *a, void const *b, void *data)
{
  (void)data ;
  return strcmp((char const *)a, (char const *)b) ;
}

static avltree dynstorage = AVLTREE_INIT(3, 3, 8, &dynstorage_dtok, &dynstorage_cmp, &cells) ;

char const *dynstorage_add (char const *s)
{
  cell_t *p ;
  uint32_t d ;
  if (avltree_search(&dynstorage, s, &d))
  {
    p = GENSETDYN_P(cell_t, &cells, d) ;
    p->refcount++ ;
    return p->s ;
  }

  if (!gensetdyn_new(&cells, &d)) return 0 ;
  p = GENSETDYN_P(cell_t, &cells, d) ;
  p->s = strdup(s) ;
  if (!p->s) goto err0 ;
  p->refcount = 1 ;
  if (!avltree_insert(&dynstorage, d)) goto err1 ;
  return p->s ;

 err1:
  free(p->s) ;
 err0:
  gensetdyn_delete(&cells, d) ;
  return 0 ;
}

void dynstorage_remove (char const *s)
{
  uint32_t d ;
  if (avltree_search(&dynstorage, s, &d)
  {
    cell_t *p = GENSETDYN_P(cell_t, &cells, d) ;
    if (!--p->refcount)
    {
      avltree_delete(&dynstorage, s) ;
      free(p->s) ;
      gensetdyn_delete(&cells, d) ;
    }
  }
}
