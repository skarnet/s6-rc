/* ISC license. */

#include <s6-rc/db.h>

extern int s6rcd_livedir_init (char const *, char const *, char const *, char const *, int *)
extern int s6rcd_livesubdir_create (char *, char const *, char const *) ;

extern int s6rcd_db_load (s6rc_db_t *, char const *) ;
extern int s6rcd_db_read_sizes (s6rc_db_sizes_t *, char const *) ;
