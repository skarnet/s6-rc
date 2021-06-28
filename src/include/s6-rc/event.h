/* ISC license. */

#ifndef S6RC_EVENT_H
#define S6RC_EVENT_H

#include <stdint.h>

typedef struct s6rc_event_s s6rc_event_t, *s6rc_event_t_ref ;
struct s6rc_event_s
{
  uint32_t name ;
  uint8_t current : 1 ;
  uint8_t up : 1 ;
  uint8_t extra : 6 ;
} ;
#define S6RC_EVENT_ZERO { .name = 0, .current = 0, .up = 0, .extra = 0 }

#endif
