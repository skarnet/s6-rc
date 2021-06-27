/* ISC license. */

#ifndef S6RCD_SERVICE_H
#define S6RCD_SERVICE_H

#include <stdint.h>

#include <skalibs/stralloc.h>


 /* Service types and db representation in memory */

typedef enum stype_e stype_t, *stype_t_ref ;
enum stype_e
{
  STYPE_LONGRUN,
  STYPE_ONESHOT,
  STYPE_EXTERNAL,
  STYPE_BUNDLE,
  STYPE_VIRTUAL,
  STYPE_PHAIL
} ;

typedef struct common_s common_t, *common_t_ref ;
struct common_s
{
  uint32_t name ;
  uint32_t deps[2] ;
  uint32_t ndeps[2] ;
  uint32_t flag_dynamic : 1 ;
  uint32_t flag_essential : 1 ;
  uint32_t flags : 30 ;
} ;

typedef struct satomic_s satomic_t, *satomic_t_ref ;
struct satomic_s
{
  common_t common ;
  uint32_t timeout[2] ;
} ;

typedef struct oneshot_s oneshot_t, *oneshot_t_ref ;
struct oneshot_s
{
  satomic_t satomic ;
  uint32_t argc[2] ;
  uint32_t argv[2] ;
} ;

typedef struct longrun_s longrun_t, *longrun_t_ref ;
struct longrun_s
{
  satomic_t satomic ;
  uint32_t consumer ;
  uint32_t nproducers ;
  uint32_t producers ;
} ;

typedef struct external_s external_t, *external_t_ref ;
struct external_s
{
  common_t common ;
} ;

typedef struct bundle_s bundle_t, *bundle_t_ref ;
struct bundle_s
{
  common_t common ;
  uint32_t ncontents ;
  uint32_t contents ;
} ;

typedef struct virtual_s virtual_t, *virtual_t_ref ;
struct virtual_s
{
  common_t common ;
  uint32_t ncontents ;
  uint32_t contents ;
} ;

typedef struct deptype_s deptype_t, *deptype_t_ref ;
struct deptype_s
{
  uint8_t passive : 1 ;
  uint8_t soft : 1 ;
  uint8_t loose : 1 ;
} ;

typedef struct db_s db_t, *db_t_ref ;
struct db_s
{
  uint32_t n[STYPE_PHAIL] ;
  longrun_t *longruns ;
  oneshot_t *oneshots ;
  external_t *externals ;
  bundle_t *bundles ;
  virtual_t *virtuals ;
  char const **argvs ;
  uint32_t *deps[2] ;
  deptype_t *deptypes[2] ;
  uint32_t *producers ;
  char *storage ;
  uint32_t storagelen ;
} ;

extern common_t const *service_common (db_t const *, stype_t, uint32_t) ;

#endif
