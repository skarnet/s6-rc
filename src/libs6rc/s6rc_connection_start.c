/* ISC license. */

#include <skalibs/djbunix.h>
#include <skalibs/webipc.h>
#include <skalibs/textmessage.h>

#include <s6-rc/connection-common.h>
#include <s6-rc/connection.h>

int s6rc_connection_start (s6rc_connection_t *a, char const *path, tain_t const *deadline, tain_t *stamp)
{
  int fd = ipc_stream_nbcoe() ;
  if (fd < 0) return 0 ;
  if (!ipc_timed_connect(fd, path, deadline, stamp))
  {
    fd_close(fd) ;
    return 0 ;
  }
  textmessage_sender_init(&a->out, fd) ;
  textmessage_receiver_init(&a->in, fd, a->inbuf, S6RC_CONNECTION_BUFSIZE, TEXTMESSAGE_MAXLEN) ;
  return 1 ;
}
