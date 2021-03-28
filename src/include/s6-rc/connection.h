/* ISC license. */

#ifndef S6_RC_CONNECTION_H
#define S6_RC_CONNECTION_H

#include <skalibs/tai.h>
#include <skalibs/textmessage.h>

#include <s6-rc/connection-common.h>

#define S6RC_MONITOR_BUFSIZE S6RC_CONNECTION_BUFSIZE

typedef struct s6rc_monitor_s s6rc_monitor_t, *s6rc_monitor_t_ref ;
struct s6rc_monitor_s
{
  textmessage_receiver_t in ;
  char inbuf[S6RC_MONITOR_BUFSIZE] ;
}
#define S6RC_MONITOR_ZERO { .in = TEXTMESSAGE_RECEIVER_ZERO, .inbuf = "" }

extern void s6rc_connection_end (s6rc_connection_t *) ;
extern int s6rc_connection_start (s6rc_connection_t *, char const *, tain_t const *, tain_t *) ;
#define s6rc_connection_start_g(path, deadline) s6rc_connection_start(path, (deadline), &STAMP)

extern int s6rc_monitor (s6rc_connection_t *, s6rc_monitor_t *, tain_t const *, tain_t *) ;

#endif
