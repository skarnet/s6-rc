/* ISC license. */

#include <skalibs/genqdyn.h>

#include "event.h"


/* Event queue */

static genqdyn evq = GENQDYN_INIT(event_t, 0, 1) ;  /* only clean when it's empty */

int event_enqueue (event_t const *ev)
{
  return genqdyn_push(&evq, ev) ;
}

int event_pop (event_t *ev)
{
  if (!genqdyn_n(&evq)) return 0 ;
  *ev = *GENQDYN_PEEK(s6rc_event_t, &evq) ;
  return genqdyn_pop(&evq) ;
}


 /* Event processing (builtins) */

void event_handle (event_t const *ev)
{
}
