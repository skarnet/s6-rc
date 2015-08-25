/* ISC license. */

#ifndef S6RC_SERVICEDIR_H
#define S6RC_SERVICEDIR_H

extern int s6rc_servicedir_block (char const *) ;
extern int s6rc_servicedir_unblock (char const *, int) ;
extern int s6rc_servicedir_copy_offline (char const *, char const *) ;
extern int s6rc_servicedir_copy_online (char const *, char const *) ;
#define s6rc_servicedir_copy(src, dst, h) ((h) ? s6rc_servicedir_copy_online(src, dst) : s6rc_servicedir_copy_offline(src, dst))

#endif
