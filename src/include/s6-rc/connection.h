/* ISC license. */

#ifndef S6_RC_CONNECTION_H
#define S6_RC_CONNECTION_H

#include <skalibs/tai.h>
#include <skalibs/textmessage.h>

#include <s6-rc/connection-common.h>

extern int s6rc_connection_start (s6rc_connection_t *, char const *, tain_t const *, tain_t *) ;
#define s6rc_connection_start_g(path, deadline) s6rc_connection_start(path, (deadline), &STAMP)

extern void s6rc_connection_end (s6rc_connection_t *) ;

#endif
