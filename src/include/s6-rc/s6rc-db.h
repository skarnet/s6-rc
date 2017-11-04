/* ISC license. */

#ifndef S6RC_DB_H
#define S6RC_DB_H

#include <stdint.h>
#include <skalibs/diuint32.h>
#include <skalibs/buffer.h>

#define S6RC_DB_BANNER_START "s6rc-db: start\n"
#define S6RC_DB_BANNER_START_LEN (sizeof(S6RC_DB_BANNER_START) - 1)
#define S6RC_DB_BANNER_END "\ns6rc-db: end\n"
#define S6RC_DB_BANNER_END_LEN (sizeof(S6RC_DB_BANNER_END) - 1)


typedef struct s6rc_oneshot_s s6rc_oneshot_t, *s6rc_oneshot_t_ref ;
struct s6rc_oneshot_s
{
  uint32_t argc[2] ;
  uint32_t argv[2] ;
} ;

typedef struct s6rc_longrun_s s6rc_longrun_t, *s6rc_longrun_t_ref ;
struct s6rc_longrun_s
{
  uint32_t consumer ;
  uint32_t nproducers ;
  uint32_t producers ;
} ;

typedef union s6rc_longshot_u s6rc_longshot_t, *s6rc_longshot_t_ref ;
union s6rc_longshot_u
{
  s6rc_oneshot_t oneshot ;
  s6rc_longrun_t longrun ;
} ;

typedef struct s6rc_service_s s6rc_service_t, *s6rc_service_t_ref ;
struct s6rc_service_s
{
  uint32_t name ;
  uint32_t flags ;
  uint32_t deps[2] ;
  uint32_t ndeps[2] ;
  uint32_t timeout[2] ;
  s6rc_longshot_t x ;
} ;

typedef struct s6rc_db_s s6rc_db_t, *s6rc_db_t_ref ;
struct s6rc_db_s
{
  s6rc_service_t *services ;
  unsigned int nshort ;
  unsigned int nlong ;
  unsigned int stringlen ;
  unsigned int nargvs ;
  unsigned int ndeps ;
  unsigned int nproducers ;
  char *string ;
  char const **argvs ;
  uint32_t *deps ;
  uint32_t *producers ;
} ;

extern int s6rc_db_read_uint32 (buffer *, uint32_t *) ;

extern int s6rc_db_read_sizes (int, s6rc_db_t *) ;
extern int s6rc_db_read (int, s6rc_db_t *) ;

extern int s6rc_db_check_pipelines (s6rc_db_t const *, diuint32 *) ;
extern int s6rc_db_check_depcycles (s6rc_db_t const *, int, diuint32 *) ;
extern int s6rc_db_check_revdeps (s6rc_db_t const *) ;

#endif
