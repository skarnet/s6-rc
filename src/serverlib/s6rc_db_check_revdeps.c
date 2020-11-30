/* ISC license. */

#include <string.h>
#include <skalibs/bitarray.h>
#include <s6-rc/s6rc-db.h>

int s6rc_db_check_revdeps (s6rc_db_t const *db)
{
  size_t n = db->nshort + db->nlong ;
  size_t m = bitarray_div8(n) ;
  unsigned char matrix[n * m] ;
  unsigned int i = n ;
  unsigned char const *p = matrix ;
  memset(matrix, 0, n * m) ;
  while (i--)
  {
    unsigned int j = db->services[i].ndeps[1] ;
    while (j--) bitarray_not(matrix + m * i, db->deps[db->ndeps + db->services[i].deps[1] + j], 1) ;
  }
  i = n ;
  while (i--)
  {
    unsigned int j = db->services[i].ndeps[0] ;
    while (j--) bitarray_not(matrix + m * db->deps[db->services[i].deps[0] + j], i, 1) ;
  }
  n *= m ;
  while (n--) if (*p++) return 1 ;
  return 0 ;
}
