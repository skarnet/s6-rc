/* ISC license. */

#ifndef S6RC_DB_H
#define S6RC_DB_H

#include <skalibs/uint32.h>
#include <skalibs/buffer.h>

#define S6RC_DB_BANNER_START "s6rc-db: start\n"
#define S6RC_DB_BANNER_START_LEN (sizeof(S6RC_DB_BANNER_START) - 1)
#define S6RC_DB_BANNER_END "\ns6rc-db: end\n"
#define S6RC_DB_BANNER_END_LEN (sizeof(S6RC_DB_BANNER_END) - 1)


typedef struct s6rc_oneshot_s s6rc_oneshot_t, *s6rc_oneshot_t_ref ;
struct s6rc_oneshot_s
{
  uint32 argc[2] ;
  uint32 argv[2] ;
} ;

typedef struct s6rc_longrun_s s6rc_longrun_t, *s6rc_longrun_t_ref ;
struct s6rc_longrun_s
{
  uint32 servicedir ;
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
  uint32 name ;
  uint32 flags ;
  uint32 deps[2] ;
  uint32 ndeps[2] ;
  uint32 timeout[2] ;
  s6rc_longshot_t x ;
  unsigned char type : 1 ;
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
  char *string ;
  char **argvs ;
  uint32 *deps ;
} ;

extern int s6rc_db_read_uint32 (buffer *, uint32 *) ;

extern int s6rc_db_read_sizes (int, s6rc_db_t *) ;
extern int s6rc_db_read (int, s6rc_db_t *) ;

extern unsigned int s6rc_db_check_depcycles (s6rc_db_t const *, int, unsigned int *) ;
extern int s6rc_db_check_revdeps (s6rc_db_t const *) ;

#endif
