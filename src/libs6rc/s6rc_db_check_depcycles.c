/* ISC license. */

#include <skalibs/uint32.h>
#include <skalibs/bitarray.h>
#include <skalibs/bytestr.h>
#include <s6-rc/s6rc-db.h>

typedef struct recinfo_s recinfo_t, *recinfo_t_ref ;
struct recinfo_s
{
  s6rc_db_t const *db ;
  unsigned int n ;
  unsigned char *gray ;
  unsigned char *black ;
  unsigned char h : 1 ;
} ;

static unsigned int s6rc_db_checknocycle_rec (recinfo_t *recinfo, unsigned int i)
{
  if (!bitarray_peek(recinfo->black, i))
  {
    unsigned int j = recinfo->db->services[i].ndeps[recinfo->h] ;
    if (bitarray_peek(recinfo->gray, i)) return i ;
    bitarray_set(recinfo->gray, i) ;
    while (j--)
    {
      register unsigned int r = s6rc_db_checknocycle_rec(recinfo, recinfo->db->deps[recinfo->h * recinfo->db->ndeps + recinfo->db->services[i].deps[recinfo->h] + j]) ;
      if (r < recinfo->n) return r ;
    }
    bitarray_set(recinfo->black, i) ;
  }
  return recinfo->n ;
}

unsigned int s6rc_db_check_depcycles (s6rc_db_t const *db, int h, unsigned int *problem)
{
  unsigned int n = db->nshort + db->nlong ;
  unsigned char gray[bitarray_div8(n)] ;
  unsigned char black[bitarray_div8(n)] ;
  recinfo_t info = { .db = db, .n = n, .gray = gray, .black = black, .h = !!h } ;
  unsigned int i = n ;
  byte_zero(gray, bitarray_div8(n)) ;
  byte_zero(black, bitarray_div8(n)) ;
  while (i--)
  {
    register unsigned int r = s6rc_db_checknocycle_rec(&info, i) ;
    if (r < n) return (*problem = r, i) ;
  }
  return n ;
}
