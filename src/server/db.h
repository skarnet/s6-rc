/* ISC license. */

#ifndef S6RCD_DB_H
#define S6RCD_DB_H

#include <s6-rc/db.h>

 /* Service database */

extern int db_load (s6rc_db_t *, char const *) ;
extern int db_read_sizes (s6rc_db_sizes_t *, char const *) ;

#endif
