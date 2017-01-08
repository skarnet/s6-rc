/* ISC license. */

#include <stdint.h>
#include <skalibs/diuint32.h>
#include <skalibs/bytestr.h>
#include <skalibs/bitarray.h>
#include <s6-rc/s6rc-db.h>

int s6rc_db_check_pipelines (s6rc_db_t const *db, diuint32 *problem)
{
  uint32_t i = db->nlong ;
  unsigned char black[bitarray_div8(db->nlong)] ;
  byte_zero(black, bitarray_div8(db->nlong)) ;
  while (i--) if (!bitarray_peek(black, i))
  {
    uint32_t j = i ;
    uint32_t start ;
    for (;;)
    {
      register uint32_t k = db->services[j].x.longrun.pipeline[0] ;
      if (k >= db->nlong) break ;
      if (k == i || bitarray_peek(black, k))
      {
        problem->left = i ;
        problem->right = k ;
        return 1 + (k == i) ;
      }
      j = k ;
    }
    start = j ;
    j = i ;
    for (;;)
    {
      register uint32_t k = db->services[j].x.longrun.pipeline[1] ;
      if (k >= db->nlong) break ;
      if (k == i || bitarray_peek(black, k))
      {
        problem->left = i ;
        problem->right = k ;
        return 1 + (k == i) ;
      }
      j = k ;
    }
    for (j = start ; j > db->nlong ; j = db->services[j].x.longrun.pipeline[1])
      bitarray_set(black, j) ;
  }
  return 0 ;
}
