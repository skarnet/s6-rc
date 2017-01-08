/* ISC license. */

#ifndef S6RC_UTILS_H
#define S6RC_UTILS_H

#include <sys/types.h>
#include <skalibs/stralloc.h>
#include <s6-rc/s6rc-db.h>

extern void s6rc_graph_closure (s6rc_db_t const *, unsigned char *, unsigned int, int) ;
extern int s6rc_lock (char const *, int, int *, char const *, int, int *) ;
extern int s6rc_read_uint (char const *, unsigned int *) ;
extern int s6rc_sanitize_dir (stralloc *, char const *, size_t *) ;

#endif
