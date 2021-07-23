/* ISC license. */

#include <sys/mman.h>
#include <errno.h>

#include <skalibs/alloc.h>
#include <skalibs/cdb.h>

#include <s6-rc/db.h>

void s6rc_db_free (s6rc_db_t *db)
{
  int e = errno ;
  alloc_free(db->argvs) ;
  cdb_free(&db->resolve) ;
  munmap(db->map, db->size) ;
  db->map = 0 ;
  errno = e ;
}
