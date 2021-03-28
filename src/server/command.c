/* ISC license. */

#include <errno.h>
#include <sys/uio.h>

#include <skalibs/uint32.h>
#include <skalibs/textmessage.h>
#include <skalibs/posixishard.h>

#include "client.h"
#include "command.h"

static inline int answer (client_t *c, char e)
{
  return textmessage_put(&c->connection.out, &e, 1) ;
}

static int do_query (client_t *c, char const *s, size_t len)
{
  if (!len--) return (errno = EPROTO, 0) ;
  return 1 ;
}

static inline int do_monitor_stop (client_t *c)
{
  monitor_finish(c) ;
  return answer(0) ;
}

static inline int do_monitor_start (client_t *c, uint32_t v)
{
  if (textmessage_sender_fd(&c->monitor) >= 0) return answer(c, EBUSY) ;
  if (!answer(c, 0)) return 0 ;

  /*
     XXX: due to the nature of ancil, we have to do a couple sync
     writes here, which may mess up our async engine. But in practice,
     with such small data, kernel buffers make it so writes/sendmsgs
     are always instantly accepted, so we get away with it.
  */

  tain_t deadline ;
  tain_addsec_g(&deadline, 2) ;
  if (!textmessage_sender_timed_flush_g(&c->connection.out, &deadline)
   || !textmessage_create_send_channel_g(textmessage_sender_fd(&c->connection.out), &c->monitor, S6RC_MONITOR_BANNER, S6RC_MONITOR_BANNERLEN, &deadline))
    return 0 ;
  c->monitor_verbosity = v ;
  client_monitors++ ;
  return 1 ;
}

static int do_monitor (client_t *c, char const *s, size_t len)
{
  if (!len--) return (errno = EPROTO, 0) ;
  if (!(c->flags & 2)) return answer(c, EPERM) ;
  switch (*s++)
  {
    case '+' :
    {
      uint32_t v ;
      if (len != 4) return (errno = EPROTO, 0) ;
      uint32_unpack_big(s, &v) ;
      return do_monitor_start(c, v) ;
    }
    case '-' :
      if (len) return (errno = EPROTO, 0) ;
      return do_monitor_stop(c, s, len) ;
    default : return (errno = EPROTO, 0) ;
  }
}

static int do_change (client_t *c, char const *s, size_t len)
{
  return 1 ;
}

static int do_event (client_t *c, char const *s, size_t len)
{
  return 1 ;
}

static int do_admin (client_t *c, char const *s, size_t len)
{
  return 1 ;
}

int command_handle (struct iovec const *v, void *aux)
{
  client_t *c = aux ;
  char const *s = v->iov_base ;
  size_t len = v->iov_len ;
  if (!len--) return (errno = EPROTO, 0) ;
  switch (*s++)
  {
    case 'Q' : return do_query(c, s, len) ;
    case 'M' : return do_monitor(c, s, len) ;
    case '?' : return do_change(c, s, len) ;
    case '!' : return do_event(c, s, len) ;
    case '#' : return do_admin(c, s, len) ;
    default : return (errno = EPROTO, 0) ;
  }
}
