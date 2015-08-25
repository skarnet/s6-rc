/* ISC license. */

#ifndef S6RC_UTILS_H
#define S6RC_UTILS_H

#include <s6-rc/s6rc-db.h>

extern void s6rc_graph_closure (s6rc_db_t const *, unsigned char *, unsigned int, int) ;
extern int s6rc_read_uint (char const *, unsigned int *) ;

#endif
