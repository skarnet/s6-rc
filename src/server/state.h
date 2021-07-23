/* ISC license. */

#ifndef S6RCD_STATE_H
#define S6RCD_STATE_H

#include <stdint.h>

#include <skalibs/genalloc.h>

#include "db.h"

 /* Service states, instances, machine state */

#define SSTATE_TRANSITIONING 0x01u
#define SSTATE_FAILED 0x02u
#define SSTATE_CURRENT 0x04u
#define SSTATE_WANTED 0x08u
#define SSTATE_EXPLICIT 0x10u
#define SSTATE_INVALID 0x80u

typedef struct sstate_s sstate_t, *sstate_t_ref ;
struct sstate_s
{
  uint8_t bits ;  /* the real state, saved to the fs */
  uint8_t tmp ;  /* bits used for computations e.g. recursive dep walks */
}
#define SSTATE_ZERO { .bits = 0, .tmp = 0 }

typedef struct instance_s instance_t, *instance_t_ref ;
struct instance_s
{
  char const *param ;  /* refcounted pointer to dynstorage */
  sstate_t sstate ;
} ;

typedef struct mstate_s mstate_t, *mstate_t_ref ;
struct mstate_s
{
  sstate_t *sta[S6RC_STYPE_N] ;
  genalloc *dyn[S6RC_STYPE_N] ;  /* every genalloc is a list of instance_t */
} ;

typedef int sstate_func (sstate_t *, void *) ;
typedef sstate_func *sstate_func_ref ;
typedef int instancelen_func (uint32_t, void *) ;
typedef instancelen_func *instancelen_func_ref ;
typedef instance_func (instance_t *, void *) ;
typedef instance_func *instance_func_ref ;

extern instancelen_func_ref const instancelen_nop ;

extern sstate_t *instance_create (mstate_t *, uint32_t, char const *) ;
extern void instance_remove (mstate_t *, uint32_t, char const *) ;

extern void mstate_free (mstate_t *, uint32_t const *) ;
extern int mstate_init (mstate_t *, uint32_t const *) ;

extern int mstate_write (char const *, mstate_t const *, uint32_t const *) ;
extern int mstate_read (char const *, mstate_t *, uint32_t const *) ; /* also inits */

extern int mstate_iterate (mstate_t *, uint32_t const *, sstate_func_ref, instancelen_func_ref, instance_func_ref, void *) ;
extern void mstate_zerotmp (mstate_t *, uint32_t const *) ;

extern sstate_t *sstate_tn (mstate_t *, uint8_t, uint32_t, char const *) ;
extern sstate_t *sstate (uint32_t const *, mstate_t *, uint32_t, char const *) ;

#endif
