/* ISC license. */

#include <fcntl.h>

#include <skalibs/types.h>
#include <skalibs/strerr2.h>
#include <skalibs/subgetopt.h>
#include <skalibs/tai.h>
#include <skalibs/djbunix.h>

#include "s6-rcd.h"

#define USAGE "s6-rcd [ -v verbosity ] [ -1 ] [ -t timeout ] [ -T lameducktimeout ] [ -i rulesdir | -x rulesfile ] [ -l livedir ]"
#define dieusage() strerr_dieusage(100, USAGE) ;

unsigned int verbosity = 1 ;
globalflags_t flags =
{
  .lameduck = 0 ;
} ;

tain_t lameduckdeadline = TAIN_INFINITE_RELATIVE ;

int main (int argc, char const *const *argv)
{
  int spfd, sock ;
  PROG = "s6-rcd" ;

  {
    char const *rules = 0 ;
    unsigned int rulestype = 0 ;
    int flag1 = 0 ;
    unsigned int t = 0, T = 0 ;
    subgetopt_t l = SUBGETOPT_ZERO ;
    for (;;)
    {
      int opt = subgetopt_r(argc, argv, "v:1c:n:i:x:t:T:", &l) ;
      if (opt == -1) break ;
      switch (opt)
      {
        case 'v' : if (!uint0_scan(l.arg, &verbosity)) dieusage() ; break ;
        case '1' : flag1 = 1 ; break ;
        case 'i' : rules = l.arg ; rulestype = 1 ; break ;
        case 'x' : rules = l.arg ; rulestype = 2 ; break ;
        case 't' : if (!uint0_scan(l.arg, &t)) dieusage() ; break ;
        case 'T' : if (!uint0_scan(l.arg, &T)) dieusage() ; break ;
        default : dieusage() ;
      }
    }
    argc -= l.ind ; argv += l.ind ;
    if (t) tain_from_millisecs(&client_answer_tto, t) ;
    if (T) tain_from_millisecs(&lameduckdeadline, T) ;
    if (!rulestype) strerr_dief1x(100, "no access rights specified!") ;
    if (flag1)
    {
      if (fcntl(1, F_GETFD) < 0)
        strerr_dief1sys(100, "called with option -1 but stdout said") ;
    }
    else close(1) ;

    spfd = signals_init() ;
    clientrules_init(rulestype, rules) ;
    sock = livedir_init() ;
    if (!tain_now_set_stopwatch_g())
      strerr_diefu1sys(111, "initialize clock") ;
    if (flag1)
    {
      fd_write(1, "\n", 1) ;
      fd_close(1) ;
    }
  }

  for (;;)
  {
    iopause_fd x[2 + client_connections + client_monitors] ;
    tain_t deadline ;
    uint32_t j = 2 ;
    int r = 1 ;

    {
      s6rc_event_t ev ;
      while (ev_pop(&ev)) { ev_handle(&ev) ; ep_run(&ev) ; }
    }

    tain_add_g(&deadline, &tain_infinite_relative) ;
    if (flags.lameduck) deadline = lameduckdeadline ;

    x[0].fd = spfd ;
    x[0].events = IOPAUSE_READ ;
    x[1].fd = sock ;
    x[1].events = !flags.lameduck ? IOPAUSE_READ : 0 ;

    for (client_t *c = client_head ; c ; c = c->next)
      if (client_prepare_iopause(c, &deadline, x, &j)) r = 0 ;

    if (r)  /* clean buffers */
    {
      if (flags.lameduck) break ;
      if (flags.dbupdate && db_update())
      {
        flags.dbupdate = 0 ;
        continue ;
      }
    }

    r = iopause_g(x, j, &deadline) ;
    if (r < 0) strerr_diefu1sys(111, "iopause") ;

    if (!r)
    {
      if (flags.lameduck && !tain_future(&lameduckdeadline)) break ;
      for (client_t *c = client_head ; c ; c = c ? c->next : client_head)
        if (!tain_future(c->deadline)) client_yoink(&c) ;
      continue ;
    }

    if (x[0].revents & IOPAUSE_READ) signals_handle() ;

    for (client_t *c = client_head ; c ; c = c ? c->next : client_head)
      if (!client_flush(c, x)) client_yoink(&c) ;
 
    for (client_t *c = client_head ; c ; c = c ? c->next : client_head)
      if (!client_read(c, x)) client_yoink(&c) ;

    if (x[1].revents & IOPAUSE_READ)
    {
      uint8_t perms = 0 ;
      int dummy ;
      int fd = ipc_accept_nb(x[1].fd, 0, 0, &dummy) ;
      if (fd < 0)
        if (!error_isagain(errno)) strerr_diefu1sys(111, "accept connection") ;
        else continue ;
      else if (!clientrules_check(fd, &perms)) fd_close(fd) ;
      else client_add(fd, perms) ;
    }
  }

  return 0 ;
}
