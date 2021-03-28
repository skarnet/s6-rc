/* ISC license. */

#ifndef S6RC_EVENT_H
#define S6RC_EVENT_H

#include <stdint.h>

typedef enum s6rc_eventtype_e s6rc_eventtype_t, *s6rc_eventtype_t_ref ;
enum s6rc_eventtype_e
{
  S6RC_EVENTTYPE_WANTED_STATE_DOWN,
  S6RC_EVENTTYPE_WANTED_STATE_UP,
  S6RC_EVENTTYPE_CURRENT_STATE_DOWN,
  S6RC_EVENTTYPE_CURRENT_STATE_UP,
  S6RC_EVENTTYPE_TRANSITION_STOP,
  S6RC_EVENTTYPE_TRANSITION_START,
  S6RC_EVENTTYPE_CUSTOM,  /* for misc events from other processes */
  S6RC_EVENTTYPE_PHAIL
} ;

typedef struct s6rc_event_s s6rc_event_t, *s6rc_event_t_ref ;
struct s6rc_event_s
{
  char const *name ;
  uint32_t value ;
  uint8_t type : 3 ;
  uint8_t extra : 5 ;
} ;
#define S6RC_EVENT_ZERO { .name = 0, .value = 0, .type = S6RC_EVENTTYPE_PHAIL, .extra = 0 }

#endif
