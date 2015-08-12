/* ISC license. */

#include <skalibs/uint32.h>
#include <skalibs/diuint32.h>
#include <skalibs/bitarray.h>
#include <skalibs/bytestr.h>
#include <s6-rc/s6rc-db.h>

typedef struct recinfo_s recinfo_t, *recinfo_t_ref ;
struct recinfo_s
{
  s6rc_db_t const *db ;
  uint32 n ;
  unsigned char *gray ;
  unsigned char *black ;
  unsigned char h : 1 ;
} ;

static uint32 s6rc_db_checknocycle_rec (recinfo_t *recinfo, uint32 i)
{
  if (!bitarray_peek(recinfo->black, i))
  {
    uint32 j = recinfo->db->services[i].ndeps[recinfo->h] ;
    if (bitarray_peek(recinfo->gray, i)) return i ;
    bitarray_set(recinfo->gray, i) ;
    while (j--)
    {
      register uint32 r = s6rc_db_checknocycle_rec(recinfo, recinfo->db->deps[recinfo->h * recinfo->db->ndeps + recinfo->db->services[i].deps[recinfo->h] + j]) ;
      if (r < recinfo->n) return r ;
    }
    bitarray_set(recinfo->black, i) ;
  }
  return recinfo->n ;
}

int s6rc_db_check_depcycles (s6rc_db_t const *db, int h, diuint32 *problem)
{
  uint32 n = db->nshort + db->nlong ;
  uint32 i = n ;
  unsigned char gray[bitarray_div8(n)] ;
  unsigned char black[bitarray_div8(n)] ;
  recinfo_t info = { .db = db, .n = n, .gray = gray, .black = black, .h = !!h } ;
  byte_zero(gray, bitarray_div8(n)) ;
  byte_zero(black, bitarray_div8(n)) ;
  while (i--)
  {
    register uint32 r = s6rc_db_checknocycle_rec(&info, i) ;
    if (r < n)
    {
      problem->left = i ;
      problem->right = r ;
      return 1 ;
    }
  }
  return 0 ;
}
