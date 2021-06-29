/* ISC license. */

#ifndef S6RCD_SERVICE_H
#define S6RCD_SERVICE_H

#include <stdint.h>


 /* Service types and db representation in memory */

#define S6RC_INSTANCES_MAX 0xffffffU
#define S6RC_INSTANCEPARAM_MAXLEN 0xffffffU

typedef enum s6rc_stype_e s6rc_stype_t, *s6rc_stype_t_ref ;
enum s6rc_stype_e
{
  S6RC_STYPE_LONGRUN,
  S6RC_STYPE_ONESHOT,
  S6RC_STYPE_EXTERNAL,
  S6RC_STYPE_BUNDLE,
  S6RC_STYPE_VIRTUAL,
  S6RC_STYPE_PHAIL
} ;


typedef struct s6rc_baseid_s s6rc_baseid_t, *s6rc_baseid_t_ref ;
struct s6rc_baseid_s
{
  uint32_t i ;
  s6rc_stype_t stype ;
} ;

typedef struct s6rc_id_s s6rc_id_t, *s6rc_id_t_ref ;
struct s6rc_id_s
{
  char const *param ;
  s6rc_baseid_t baseid ;
} ;

typedef struct s6rc_common_s s6rc_common_t, *s6rc_common_t_ref ;
struct s6rc_common_s
{
  uint32_t name ;
  uint32_t deps[2] ;
  uint32_t ndeps[2] ;
  uint32_t flag_essential : 1 ;
  uint32_t flags : 31 ;
} ;

typedef struct s6rc_atomic_s s6rc_atomic_t, *s6rc_atomic_t_ref ;
struct s6rc_atomic_s
{
  s6rc_common_t common ;
  uint32_t timeout[2] ;
} ;

typedef struct s6rc_oneshot_s s6rc_oneshot_t, *s6rc_oneshot_t_ref ;
struct s6rc_oneshot_s
{
  s6rc_atomic_t satomic ;
  uint32_t argc[2] ;
  uint32_t argv[2] ;
} ;

typedef struct s6rc_longrun_s s6rc_longrun_t, *s6rc_longrun_t_ref ;
struct s6rc_longrun_s
{
  s6rc_atomic_t satomic ;
  s6rc_sid_t consumer ;
  uint32_t nproducers ;
  uint32_t producers ;
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

typedef struct s6rc_deptype_s s6rc_deptype_t, *s6rc_deptype_t_ref ;
struct s6rc_deptype_s
{
  uint8_t passive : 1 ;
  uint8_t soft : 1 ;
  uint8_t loose : 1 ;
} ;

typedef struct s6rc_db_s s6rc_db_t, *s6rc_db_t_ref ;
struct s6rc_db_s
{
  uint32_t n[STYPE_PHAIL << 1] ;
  s6rc_longrun_t *longruns ;
  s6rc_oneshot_t *oneshots ;
  s6rc_external_t *externals ;
  s6rc_bundle_t *bundles ;
  s6rc_bundle_t *virtuals ;
  s6rc_baseid_t *deps[2] ;
  s6rc_deptype_t *deptypes[2] ;
  s6rc_baseid_t *producers ;
  char const **argvs ;
  char *storage ;
} ;

extern s6rc_common_t const *s6rc_service_common (s6rc_db_t const *, s6rc_sid_t const *) ;

#endif
