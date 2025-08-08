/* ISC license. */

#ifndef S6RC_REPO_H
#define S6RC_REPO_H

#include <stddef.h>
#include <stdint.h>

#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

extern char const s6rc_repo_sublist[3][7] ;
extern int s6rc_repo_fillset (char const *, char const *) ;

extern int s6rc_repo_cleanup (char const *) ;
extern int s6rc_repo_sync (char const *, char const *const *, size_t, unsigned int, char const *) ;
extern int s6rc_repo_lock (char const *, int) ;

extern int s6rc_repo_makesetbundles (char const *, char const *, unsigned int) ;
extern int s6rc_repo_makedefbundle (char const *, char const *, char const *) ;

#define S6RC_REPO_COMPILE_BUFLEN(repolen, setlen) ((repolen) + (setlen) + 45)
extern int s6rc_repo_compile (char const *, char const *, char const *const *, size_t, char *, unsigned int, char const *) ;
extern int s6rc_repo_refcompile (char const *, char *, unsigned int, char const *) ;
extern int s6rc_repo_setcompile (char const *, char const *, char const *, char *, unsigned int, char const *) ;

extern int s6rc_repo_listsub (char const *, char const *, char const *, stralloc *, genalloc *) ;
extern int s6rc_repo_listcontents (char const *, char const *, stralloc *, genalloc *) ;
extern int s6rc_repo_listdeps (char const *, char const *, stralloc *, genalloc *, int) ;
extern int s6rc_repo_listalldeps (char const *, char const *const *, size_t, stralloc *, genalloc *, int) ;
extern int s6rc_repo_listdeps_internal (char const *, char const *const *, size_t, stralloc *, genalloc *, uint32_t) ;

extern int s6rc_repo_moveservices (char const *, char const *, char const *const *, size_t, char const *, char const *, unsigned int) ;

#endif
