/* ISC license. */

#ifndef S6RCD_EV_H
#define S6RCD_EV_H

#include <s6-rc/event.h>


 /* Event queue */

extern int ev_enqueue (s6rc_event_t const *) ;
extern int ev_pop (s6rc_event_t *) ;


 /* Event processor: the static part */

extern void ev_handle (s6rc_event_t const *) ;

#endif
