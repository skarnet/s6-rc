/* ISC license. */

#ifndef S6RCD_SERVICE_H
#define S6RCD_SERVICE_H

#include <sys/types.h>
#include <stdint.h>

#include <skalibs/cdb.h>

#define S6RC_ARGV_MAX 0x00ffffffu
#define S6RC_INSTANCES_MAX 0x00ffffffu
#define S6RC_INSTANCEPARAM_MAXLEN 0x00ffffffu

 /* Service types and db representation in memory */

#define S6RC_STYPE_LONGRUN 0
#define S6RC_STYPE_ONESHOT 1
#define S6RC_STYPE_EXTERNAL 2
#define S6RC_STYPE_BUNDLE 3
#define S6RC_STYPE_VIRTUAL 4
#define S6RC_STYPE_N 5

typedef struct s6rc_common_s s6rc_common_t, *s6rc_common_t_ref ;
struct s6rc_common_s
{
  uint32_t name ;
  uint32_t flags ;
  uint32_t deps[2] ;
  uint32_t ndeps[2] ;
} ;

#define S6RC_DB_FLAG_ESSENTIAL 0x80000000u
#define S6RC_DB_FLAG_WEAK 0x40000000u

 /*
   s6rc_common_t must always be the first field of a service,
   so we can cast the pointer to s6rc_longrun_t * and friends
   in a C-approved way.
 */

typedef struct s6rc_atomic_s s6rc_atomic_t, *s6rc_atomic_t_ref ;
struct s6rc_atomic_s
{
  s6rc_common_t common ;
  uint32_t timeout[2] ;
} ;

typedef struct s6rc_longrun_s s6rc_longrun_t, *s6rc_longrun_t_ref ;
struct s6rc_longrun_s
{
  s6rc_atomic_t satomic ;
  uint32_t nproducers ;
  uint32_t producers ;
  uint32_t consumer ;
} ;

typedef struct s6rc_oneshot_s s6rc_oneshot_t, *s6rc_oneshot_t_ref ;
struct s6rc_oneshot_s
{
  s6rc_atomic_t satomic ;
  uint32_t argv[2] ;
} ;

typedef struct s6rc_external_s s6rc_external_t, *s6rc_external_t_ref ;
struct s6rc_external_s
{
  s6rc_common_t common ;
} ;

typedef struct s6rc_bundle_s s6rc_bundle_t, *s6rc_bundle_t_ref ;
struct s6rc_bundle_s
{
  s6rc_common_t common ;
  uint32_t ncontents ;
  uint32_t contents ;
} ;

typedef struct s6rc_db_s s6rc_db_t, *s6rc_db_t_ref ;
struct s6rc_db_s
{
  char const *map ;
  off_t size ;
  uint32_t const *n ;
  s6rc_longrun_t const *longruns ;
  s6rc_oneshot_t const *oneshots ;
  s6rc_external_t const *externals ;
  s6rc_bundle_t const *bundles ;
  s6rc_bundle_t const *virtuals ;
  uint32_t const *deps[2] ;
  uint8_t const *deptypes[2] ;
  uint32_t const *producers ;
  char const *storage ;
  char const **argvs ;  /* alloced */
} ;
#define S6RC_DB_ZERO = { .map = 0, .len = 0 }

extern uint32_t s6rc_service_id (uint32_t const *, uint8_t, uint32_t) ;
extern void s6rc_service_typenum (uin32_t const *, uint32_t, uint8_t *, uint32_t *) ;
extern uint8_t s6rc_service_type (uint32_t const *, uint32_t) ;
extern uint32_t s6rc_service_num (uint32_t const *, uint32_t) ;

extern int s6rc_service_resolve (cdb_t *, char const *, uint32_t *, char const **) ;
extern s6rc_common_t const *s6rc_service_common (s6rc_db_t const *, uint32_t) ;
extern s6rc_common_t const *s6rc_service_common_tn (s6rc_db_t const *, uint8_t, uint32_t) ;
extern int s6rc_service_recheck_instance (s6rc_db_t const *, cdb_t *, uint32_t *, char const **) ;

#endif
