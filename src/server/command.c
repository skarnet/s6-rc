/* ISC license. */

#include <errno.h>
#include <sys/uio.h>

#include <skalibs/textmessage.h>
#include <skalibs/posixishard.h>

#include "client.h"
#include "command.h"

static int answer (client_t *c, char e)
{
  if (!textmessage_put(&c->connection.out, &e, 1)) return 0 ;
  client_setdeadline(c) ;
  return 1 ;
}

static int do_query (client_t *c, char const *s, size_t len)
{
  if (!len--) return (errno = EPROTO, 0) ;
}

static int do_monitor (client_t *c, char const *s, size_t len)
{
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
    default : return do_error(c) ;
  }
}
