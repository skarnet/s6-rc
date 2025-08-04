/* ISC license. */

#ifndef S6RC_REPO_H
#define S6RC_REPO_H

#include <stddef.h>
#include <stdint.h>

#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

extern int s6rc_repo_cleanup (char const *) ;
extern int s6rc_repo_sync (char const *, char const *const *, size_t, unsigned int, char const *) ;
extern int s6rc_repo_lock (char const *, int) ;

#define S6RC_REPO_SET_COMPILE_BUFLEN(repolen, setlen) ((repolen) + (setlen) + 45)
extern int s6rc_repo_set_makebundle (char const *, char const *, char const *, char const *) ;
extern int s6rc_repo_set_compile (char const *, char const *, char const *const *, size_t, char *, unsigned int, char const *) ;
extern int s6rc_repo_set_defcompile (char const *, char const *, char const *, char *, unsigned int, char const *) ;

extern int s6rc_repo_set_listdeps (char const *, char const *, stralloc *, genalloc *, int) ;
extern int s6rc_repo_set_listalldeps (char const *, char const *const *, size_t, stralloc *, genalloc *, int) ;
extern int s6rc_repo_set_listdeps_internal (char const *, char const *const *, size_t, stralloc *, genalloc *, uint32_t) ;

extern int s6rc_repo_set_moveservices (char const *, char const *, char const *const *, size_t, char const *, char const *) ;

#endif
