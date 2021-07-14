/* ISC license. */

#ifndef S6RCD_STATE_H
#define S6RCD_STATE_H

#include <stdint.h>

#include <skalibs/genalloc.h>

#include "db.h"

 /* Service states, instances, machine state */

#define SSTATE_STABLE 0x00u
#define SSTATE_WAITING 0x01u
#define SSTATE_TRANSITIONING 0x02u
#define SSTATE_FAILED 0x03u

#define SSTATE_CURRENT 0x04u
#define SSTATE_WANTED 0x08u
#define SSTATE_EXPLICIT 0x10u
#define SSTATE_GRAY 0x20u
#define SSTATE_BLACK 0x40u
#define SSTATE_INVALID 0x80u

typedef struct sstate_s sstate_t, *sstate_t_ref ;
struct sstate_s
{
  uint8_t bits ;
}

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

extern void instance_free (instance_t *) ;
extern int instance_new (instance_t *, uint8_t *, char const *) ;

extern void mstate_free (mstate_t *, uint32_t const *) ;
extern int mstate_init (mstate_t *, uint32_t const *) ;

extern int mstate_write (char const *, mstate_t const *, uint32_t const *) ;
extern int mstate_read (char const *, mstate_t *, uint32_t const *) ; /* also inits */

extern sstate_t *sstate_get (s6rc_db_t const *, mstate_t const *, s6rc_id_t, char const *) ;
extern int sstate_deps_fulfilled (s6rc_db_t const *, mstate_t const *, s6rc_id_t, char const *, int) ;

#endif
