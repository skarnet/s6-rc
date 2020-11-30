/* ISC license. */

#include <skalibs/djbunix.h>
#include <skalibs/webipc.h>
#include <skalibs/textmessage.h>

#include <s6-rc/connection-common.h>
#include <s6-rc/connection.h>

int s6rc_monitor (s6rc_connection_t *a, textmessage_receiver_t *asyncin, char *asyncbuf, size_t asyncbufsize, tain_t const *deadline, tain_t *stamp)
{
  if (!textmessage_timed_send(&a->out, "M", 1, deadline, stamp)) return 0 ;
  if (!textmessage_recv_channel(textmessage_receiver_fd(&a->in), asyncin, asyncbuf, asyncbufsize, S6RC_MONITOR_BANNER, S6RC_MONITOR_BANNERLEN, deadline, stamp)) return 0 ;
  return 1 ;
}
