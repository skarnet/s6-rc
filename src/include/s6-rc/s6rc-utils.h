/* ISC license. */

#ifndef S6RC_UTILS_H
#define S6RC_UTILS_H

#include <sys/types.h>
#include <stdint.h>

#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/s6rc-db.h>

extern void s6rc_graph_closure (s6rc_db_t const *, unsigned char *, unsigned int, int) ;
extern int s6rc_lock (char const *, int, int *, char const *, int, int *, int) ;
extern int s6rc_read_uint (char const *, unsigned int *) ;
extern int s6rc_sanitize_dir (stralloc *, char const *, size_t *) ;

extern int s6rc_livedir_prefixsize (char const *, size_t *) ;
extern ssize_t s6rc_livedir_prefix (char const *, char *, size_t) ;
extern int s6rc_livedir_create (stralloc *, char const *, char const *, char const *, char const *, char const *, unsigned char const *, unsigned int, size_t *) ;
extern int s6rc_livedir_canon (char const **) ;

extern int s6rc_live_state_size (char const *, uint32_t *, uint32_t *) ;
extern int s6rc_live_state_read (char const *, unsigned char *, uint32_t) ;

extern int s6rc_type_check (int, char const *) ;
extern int s6rc_nlto0 (char *, size_t, size_t, genalloc *) ;

#endif
