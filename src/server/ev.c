/* ISC license. */

#include <skalibs/genqdyn.h>

#include <s6-rc/event.h>
#include "ev.h"


/* Event queue */

static genqdyn evq = GENQDYN_INIT(s6rc_event_t, 0, 1) ;  /* only clean when it's empty */

int ev_enqueue (s6rc_event_t const *ev)
{
  return genqdyn_push(&evq, ev) ;
}

int ev_pop (s6rc_event_t *ev)
{
  if (!genqdyn_n(&evq)) return 0 ;
  *ev = *GENQDYN_PEEK(s6rc_event_t, &evq) ;
  return genqdyn_pop(&evq) ;
}


 /* Event processing (builtins) */

void ev_handle (s6rc_event_t const *ev)
{
  switch (ev->type)
  {
    case S6RC_EVENTTYPE_WANTED_STATE_DOWN :
      break ;
    case S6RC_EVENTTYPE_WANTED_STATE_UP :
      break ;
    case S6RC_EVENTTYPE_CURRENT_STATE_DOWN :
      break ;
    case S6RC_EVENTTYPE_CURRENT_STATE_UP :
      break ;
    case S6RC_EVENTTYPE_TRANSITION_STOP :
      break ;
    case S6RC_EVENTTYPE_TRANSITION_START :
      break ;
    case S6RC_EVENTTYPE_CUSTOM :
      break ;
  }
}
