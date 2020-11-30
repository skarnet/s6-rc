/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <skalibs/bitarray.h>
#include <s6-rc/s6rc-db.h>

typedef struct recinfo_s recinfo_t, *recinfo_t_ref ;
struct recinfo_s
{
  s6rc_db_t const *db ;
  uint32_t n ;
  unsigned char *gray ;
  unsigned char *black ;
  unsigned char h : 1 ;
} ;

static uint32_t s6rc_db_checknocycle_rec (recinfo_t *recinfo, uint32_t i)
{
  if (!bitarray_peek(recinfo->black, i))
  {
    uint32_t j = recinfo->db->services[i].ndeps[recinfo->h] ;
    if (bitarray_peek(recinfo->gray, i)) return i ;
    bitarray_set(recinfo->gray, i) ;
    while (j--)
    {
      uint32_t r = s6rc_db_checknocycle_rec(recinfo, recinfo->db->deps[recinfo->h * recinfo->db->ndeps + recinfo->db->services[i].deps[recinfo->h] + j]) ;
      if (r < recinfo->n) return r ;
    }
    bitarray_set(recinfo->black, i) ;
  }
  return recinfo->n ;
}

int s6rc_db_check_depcycles (s6rc_db_t const *db, int h, diuint32 *problem)
{
  uint32_t n = db->nshort + db->nlong ;
  uint32_t i = n ;
  unsigned char gray[bitarray_div8(n)] ;
  unsigned char black[bitarray_div8(n)] ;
  recinfo_t info = { .db = db, .n = n, .gray = gray, .black = black, .h = !!h } ;
  memset(gray, 0, bitarray_div8(n)) ;
  memset(black, 0, bitarray_div8(n)) ;
  while (i--)
  {
    uint32_t r = s6rc_db_checknocycle_rec(&info, i) ;
    if (r < n)
    {
      problem->left = i ;
      problem->right = r ;
      return 1 ;
    }
  }
  return 0 ;
}
