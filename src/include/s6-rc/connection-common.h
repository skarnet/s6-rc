/* ISC license. */

#ifndef S6RC_CONNECTION_COMMON_H
#define S6RC_CONNECTION_COMMON_H

#include <skalibs/textmessage.h>

#define S6RC_CONNECTION_BUFSIZE 4096

typedef struct s6rc_connection_s s6rc_connection_t, *s6rc_connection_t_ref ;
struct s6rc_connection_s
{
  textmessage_sender_t out ;
  textmessage_receiver_t in ;
  char inbuf[S6RC_CONNECTION_BUFSIZE] ;
} ;
#define S6RC_CONNECTION_ZERO { .out = TEXTMESSAGE_SENDER_ZERO, .in = TEXTMESSAGE_RECEIVER_ZERO, .inbuf = "" }

#define S6RC_MONITOR_BANNER "s6-rc monitor: after\n"
#define S6RC_MONITOR_BANNERLEN (sizeof(S6RC_MONITOR_BANNER) - 1)

#endif
