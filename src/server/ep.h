/* ISC license. */

#ifndef S6RCD_EP_H
#define S6RCD_EP_H

#include <stdint.h>

#include "event.h"


 /* Event processor: the dynamic part */

typedef void ep_func_t (event_t const *, uint32_t, void *) ;
typedef ep_func_t *ep_func_t_ref ;

extern void ep_free (void) ;
extern int ep_add (uint8_t, char const *, uint32_t, ep_func_t_ref, void *) ;
extern void ep_delete (uint8_t, char const *, uint32_t, ep_func_t_ref, void *) ;
extern void ep_run (event_t const *) ;

#endif
