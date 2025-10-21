/* ISC license. */

#ifndef S6RC_REPO_H
#define S6RC_REPO_H

#include <stddef.h>
#include <stdint.h>

#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

typedef struct s6rc_repo_sv_s s6rc_repo_sv, *s6rc_repo_sv_ref ;
struct s6rc_repo_sv_s
{
  uint32_t pos ;
  uint8_t sub ;
} ;

extern int s6rc_repo_sv_cmpr (void const *, void const *, void *) ;
extern int s6rc_repo_sv_bcmpr (void const *a, void const *b, void *aux) ;

extern char const s6rc_repo_subnames[4][7] ;

extern int s6rc_repo_makestores (char const *, char const *const *, uint16_t, char *) ;

extern int s6rc_repo_fillset (char const *, char const *, char const *const *, uint32_t) ;

extern int s6rc_repo_cleanup (char const *) ;
extern int s6rc_repo_sync (char const *, unsigned int, char const *) ;
extern int s6rc_repo_syncset (char const *, char const *, unsigned int) ;
extern int s6rc_repo_syncset_tmp (char const *, char const *, stralloc *, genalloc *ga, unsigned int) ;
extern int s6rc_repo_lock (char const *, int) ;
extern int s6rc_repo_touch (char const *) ;
extern int s6rc_repo_touchset (char const *, char const *) ;
extern int s6rc_repo_checkset (char const *, char const *) ;
extern int s6rc_repo_setuptodate (char const *, char const *) ;

extern int s6rc_repo_makesetbundles (char const *, char const *, unsigned int) ;
extern int s6rc_repo_makedefbundle (char const *, char const *, char const *) ;

#define S6RC_REPO_COMPILE_BUFLEN(repolen, setlen) ((repolen) + (setlen) + 45)
extern int s6rc_repo_compile (char const *, char const *, char const *const *, uint8_t, char *, unsigned int, char const *) ;
extern int s6rc_repo_refcompile (char const *, char *, unsigned int, char const *) ;
extern int s6rc_repo_setcompile (char const *, char const *, char const *, char *, unsigned int, char const *) ;

extern int s6rc_repo_list_sets (char const *, stralloc *, genalloc *) ;
extern int s6rc_repo_listsub (char const *, char const *, char const *, stralloc *, genalloc *) ;
extern int s6rc_repo_listcontents (char const *, char const *, stralloc *, genalloc *) ;
extern int s6rc_repo_listdeps (char const *, char const *, stralloc *, genalloc *, int) ;
extern int s6rc_repo_listalldeps (char const *, char const *const *, uint32_t, stralloc *, genalloc *, int) ;
extern int s6rc_repo_listdeps_internal (char const *, char const *const *, uint32_t, stralloc *, genalloc *, uint32_t) ;

extern int s6rc_repo_getserviceflags (char const *, char const *, uint32_t *) ;
extern int s6rc_repo_flattenservices (char const *, char const *const *, uint32_t n, stralloc *storage, genalloc *indices) ;
extern int s6rc_repo_makesvlist (char const *, char const *, stralloc *, genalloc *, uint32_t *) ;
extern int s6rc_repo_makesvlist_byname (char const *, char const *, stralloc *, genalloc *) ;
extern int s6rc_repo_badsub (char const *, char const *, char const **, uint32_t, uint8_t, uint8_t, s6rc_repo_sv const *, uint32_t, stralloc *, genalloc *) ;
extern int s6rc_repo_moveservices (char const *, char const *, s6rc_repo_sv const *, uint32_t, uint8_t, char const *, unsigned int) ;

extern int s6rc_repo_fixset (char const *, char const *, uint32_t, unsigned int, stralloc *, genalloc *, genalloc *) ;
extern int s6rc_repo_fix (char const *, uint32_t, unsigned int) ;

#endif
