/* ISC license. */

#ifndef S6RC_REPO_H
#define S6RC_REPO_H

#include <stddef.h>

extern int s6rc_repo_cleanup (char const *) ;
extern int s6rc_repo_sync (char const *, char const *const *, size_t, unsigned int) ;

#endif
