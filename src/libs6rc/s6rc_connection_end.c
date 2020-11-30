/* ISC license. */

#include <skalibs/djbunix.h>
#include <skalibs/textmessage.h>

#include <s6-rc/connection-common.h>
#include <s6-rc/connection.h>

void s6rc_connection_end (s6rc_connection_t *a)
{
  fd_close(textmessage_sender_fd(&a->out)) ;
  textmessage_sender_free(&a->out) ;
  textmessage_receiver_free(&a->in) ;
}
