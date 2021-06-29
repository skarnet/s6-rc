/* ISC license. */

#ifndef S6RCD_STATE_H
#define S6RCD_STATE_H

#include <skalibs/genalloc.h>

#include "db.h"

 /* Service states, instances, machine state */

typedef struct stateatom_s stateatom_t, *stateatom_t_ref ;
struct stateatom_s
{
  uint8_t wanted : 1 ;
  uint8_t current : 1 ;
  uint8_t transitioning : 1 ;
} ;
#define STATEATOM_ZERO { .wanted = 0, .current = 0, .transitioning = 0 }

typedef struct instance_s instance_t, *instance_t_ref ;
struct instance_s
{
  char const *param ;  /* refcounted pointer to dynstorage */
  stateatom_t state ;
} ;

typedef struct state_s state_t, *state_t_ref ;
struct state_s
{
  stateatom_t *sta[S6RC_STYPE_PHAIL] ;
  genalloc *dyn[S6RC_STYPE_PHAIL] ;  /* every genalloc is a list of instance_t */
} ;

extern void instance_free (instance_t *) ;
extern int instance_new (instance_t *, stateatom_t const *, char const *) ;

extern void state_free (state_t *, uint32_t const *) ;
extern int state_init (state_t *, uint32_t const *) ;

extern int state_write (char const *, state_t const *, uint32_t const *) ;
extern int state_read (char const *, state_t *, uint32_t const *) ; /* also inits */

extern stateatom_t *state_atom (s6rc_db_t const *, state_t const *, s6rc_sid_t const *) ;

#endif
