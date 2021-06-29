/* ISC license. */

#include <skalibs/alloc.h>

#include "db.h"

void s6rc_db_free (s6rc_db_t *db)
{
  alloc_free(db->storage) ;
  alloc_free(db->argvs) ;
  alloc_free(db->producers) ;
  alloc_free(db->deptypes[0]) ;
  alloc_free(db->deps[0]) ;
  alloc_free(db->bundles) ;
  alloc_free(db->externals) ;
  alloc_free(db->oneshots) ;
  alloc_free(db->longruns) ;
}
