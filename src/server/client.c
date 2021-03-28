/* ISC license. */

#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <skalibs/alloc.h>
#include <skalibs/error.h>
#include <skalibs/strerr2.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>
#include <skalibs/iopause.h>
#include <skalibs/textmessage.h>

#include <s6-rc/connection-common.h>
#include "command.h"
#include "main.h"
#include "client.h"

 /*
    We can't use gensetdyn for client storage, because
    c->connection.in has a pointer to c->connection.inbuf
    and gensetdyn can relocate the whole thing on realloc;
    also, having a sentinel is too costly (client_t is big).
    So we use an OG linked list, with all the boundary value
    problems it brings.
 */

tain_t client_answer_tto = TAIN_INFINITE ;
client_t *client_head = 0 ;
uint32_t client_connections = 0 ;
uint32_t client_monitors = 0 ;

static inline int ismonitored (client_t *c)
{
  return textmessage_sender_fd(&c->monitor) >= 0 ;
}

static inline void client_free (client_t *c)
{
  monitor_finish(c) ;
  fd_close(textmessage_sender_fd(&c->connection.out)) ;
  textmessage_sender_free(&c->connection.out) ;
  textmessage_receiver_free(&c->connection.in) ;
  alloc_free(c) ;
}

void client_yoink (client_t **c)
{
  client_t *prev = (*c)->prev ;
  if (prev) prev->next = (*c)->next ; else client_head = (*c)->next ;
  if ((*c)->next) (*c)->next->prev = prev ;
  if (ismonitored(*c)) nmonitors-- ;
  client_free(*c) ;
  *c = prev ;
  client_connections-- ;
}

void client_setdeadline (client_t *c)
{
  tain_t blah ;
  tain_half(&blah, &tain_infinite_relative) ;
  tain_add_g(&blah, &blah) ;
  if (tain_less(&blah, &c->deadline))
    tain_add_g(&c->deadline, &client_answer_tto) ;
}

int client_prepare_iopause (client_t *c, tain_t *deadline, iopause_fd *x, uint32_t *j)
{
  if (tain_less(&c->deadline, deadline)) *deadline = c->deadline ;
  if (!textmessage_sender_isempty(&c->connection.out) || !textmessage_receiver_isempty(&c->connection.in) || (!flags.lameduck && !textmessage_receiver_isfull(&c->connection.in)))
  {
    x[*j].fd = textmessage_sender_fd(&c->connection.out) ;
    x[*j].events = (!textmessage_receiver_isempty(&c->connection.in) || (!flags.lameduck && !textmessage_receiver_isfull(&c->connection.in)) ? IOPAUSE_READ : 0) | (!textmessage_sender_isempty(&c->connection.out) ? IOPAUSE_WRITE : 0) ;
    c->xindex[0] = (*j)++ ;
  }
  else c->xindex[0] = 0 ;
  if (ismonitored(c) && !textmessage_sender_isempty(&c->monitor))
  {
    x[*j].fd = textmessage_sender_fd(&c->monitor) ;
    x[*j].events = IOPAUSE_WRITE ;
    c->xindex[1] = (*j)++ ;
  }
  else c->xindex[1] = 0 ;
  return !!c->xindex[0] || !!c->xindex[1] ;
}

int client_add (int fd, uint8_t perms)
{
  client_t *c = alloc(sizeof(client_t)) ;
  if (!c) return 0 ;
  c->xindex[0] = c->xindex[1] = 0 ;
  tain_add_g(&c->deadline, &client_answer_tto) ;
  c->perms = perms ;
  c->monitor = textmessage_sender_zero ;
  textmessage_sender_init(&c->connection.out, fd) ;
  textmessage_receiver_init(&c->connection.in, fd, &c->connection.inbuf, S6RC_CONNECTION_BUFSIZE) ;
  c->prev = 0 ;
  c->next = client_head ;
  if (c->next) c->next->prev = c ;
  client_head = c ;
  client_connections++ ;
  return 1 ;
}

int client_flush (client_t *c, iopause_fd const *x)
{
  int ok = 1 ;
  int done = 1 ;
  if (c->xindex[0] && (x[c->xindex[0]].revents & IOPAUSE_WRITE))
  {
    if (!textmessage_sender_flush(&c->connection.out))
    {
      done = 0 ;
      if (!error_isagain(errno)) ok = 0 ;
    }
  }
  if (ismonitored(c) && c->xindex[1] && (x[c->xindex[1]].revents & IOPAUSE_WRITE))
  {
    if (!textmessage_sender_flush(&c->monitor))
    {
      done = 0 ;
      if (!error_isagain(errno)) ok = 0 ;
    }
  }
  if (done) tain_add_g(&c->deadline, &tain_infinite_relative) ;
  return ok ;
}

int client_read (client_t *c, iopause_fd const *x)
{
  return !textmessage_receiver_isempty(&c->connection.in) || (c->xindex[0] && (x[c->xindex[0]].revents & IOPAUSE_READ)) ?
    textmessage_handle(&c->connection.in, command_handle, c) > 0 : 1 ;
}

int monitor_init (client_t *c, int fd, unsigned int v)
{
  if (ismonitored(c)) return (errno = EINVAL, 0) ;
  textmessage_sender_init(&c->monitor, fd) ;
  c->monitor_verbosity = v ;
  client_monitors++ ;
  return 1 ;
}

void monitor_finish (client_t *c)
{
  if (!ismonitored(c)) return ;
  fd_close(textmessage_sender_fd(&c->monitor)) ;
  textmessage_sender_free(&c->monitor) ;
  c->monitor = textmessage_sender_zero ;
  client_monitors-- ;
}

void monitor_put (unsigned int v, char const *s, size_t len)
{
  if (!client_monitors) return ;
  for (client_t *c = client_head ; c ; c = c->next)
    if (ismonitored(c) && v >= c->monitor_verbosity && !textmessage_put(&c->monitor, s, len))
      strerr_diefu1sys("queue message to monitor") ;
}
