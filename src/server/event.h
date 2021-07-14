/* ISC license. */

#ifndef S6RCD_EV_H
#define S6RCD_EV_H

#include "db.h"

typedef event_s event_t, *event_t_ref ;
struct event_s
{
  s6rc_id_t id ;
  char const *param ;
  uint8_t wanted : 1 ;
  uint8_t up : 1 ;
  uint8_t extra : 6 ;
} ;


 /* Event queue */

extern int event_enqueue (event_t const *) ;
extern int event_pop (event_t *) ;


 /* Event processor: the builtin part */

extern void event_handle (event_t const *) ;

#endif
