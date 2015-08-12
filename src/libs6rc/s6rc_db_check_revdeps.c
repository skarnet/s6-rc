/* ISC license. */

#include <skalibs/bytestr.h>
#include <skalibs/bitarray.h>
#include <s6-rc/s6rc-db.h>

int s6rc_db_check_revdeps (s6rc_db_t const *db)
{
  unsigned int n = db->nshort + db->nlong ;
  unsigned int m = bitarray_div8(n) ;
  unsigned char matrix[n * m] ;
  register unsigned int i = n ;
  register unsigned char const *p = matrix ;
  byte_zero(matrix, n * m) ;
  while (i--)
  {
    register unsigned int j = db->services[i].ndeps[1] ;
    while (j--) bitarray_not(matrix + m * i, db->deps[db->ndeps + db->services[i].deps[1] + j], 1) ;
  }
  i = n ;
  while (i--)
  {
    register unsigned int j = db->services[i].ndeps[0] ;
    while (j--) bitarray_not(matrix + m * db->deps[db->services[i].deps[0] + j], i, 1) ;
  }
  i = n * m ;
  while (i--) if (*p++) return 1 ;
  return 0 ;
}
