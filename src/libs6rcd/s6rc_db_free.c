/* ISC license. */

#include <errno.h>

#include <skalibs/alloc.h>

#include "db.h"

void s6rc_db_free (s6rc_db_t *db)
{
  int e = errno ;
  alloc_free(db->argvs) ;
  munmap(db->map, db->size) ;
  db->map = 0 ;
  errno = e ;
}
