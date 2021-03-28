/* ISC license. */

#ifndef S6RCD_CLIENT_H
#define S6RCD_CLIENT_H

#include <stdint.h>

#include <skalibs/tai.h>
#include <skalibs/iopause.h>
#include <skalibs/textmessage.h>

#include <s6-rc/connection.h>


 /* Client connection */

typedef struct client_s client_t, *client_t_ref ;
struct client_s
{
  client_t *prev ;
  client_t *next ;
  uint32_t xindex[2] ;
  tain_t deadline ;
  s6rc_connection_t connection ;
  textmessage_sender_t monitor ;
  unsigned int monitor_verbosity ;
  uint8_t perms ;
} ;
#define CLIENT_ZERO { .next = 0, .xindex = 0, .deadline = TAIN_ZERO, .connection = S6RC_CONNECTION_ZERO, .monitor = TEXTMESSAGE_SENDER_ZERO, .monitor_verbosity = 0, .perms = 0 }

extern tain_t client_answer_tto ;
extern client_t *client_head ;
extern uint32_t client_connections ;

extern void client_yoink (client_t **) ;
extern int client_prepare_iopause (client_t *, tain_t *, iopause_fd *, uint32_t *) ;
extern int client_flush (client_t *, iopause_fd const *) ;
extern int client_add (int, uint8_t) ;

extern void client_setdeadline (client_t *) ;


 /* Monitors */

extern uint32_t client_monitors ;
extern int monitor_init (client_t *, int) ;
extern int monitor_finish (client_t *) ;
extern int monitor_put (unsigned int, char const *, size_t) ;

#endif
