/* ISC license. */

#ifndef S6RC_REPO_H
#define S6RC_REPO_H

#include <stddef.h>

extern int s6rc_repo_cleanup (char const *) ;
extern int s6rc_repo_sync (char const *, char const *const *, size_t, unsigned int, char const *) ;
extern int s6rc_repo_lock (char const *, int) ;

#define S6RC_REPO_SET_COMPILE_BUFLEN(repolen, setlen) ((repolen) + (setlen) + 45)
extern int s6rc_repo_set_compile (char const *, char const *, char const *const *, size_t, char *, unsigned int, char const *) ;

#endif
