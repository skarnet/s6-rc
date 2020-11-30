/* ISC license. */

#include <string.h>
#include <skalibs/bitarray.h>
#include <s6-rc/s6rc-db.h>
#include <s6-rc/s6rc-utils.h>

typedef struct recinfo_s recinfo_t, *recinfo_t_ref ;
struct recinfo_s
{
  s6rc_db_t const *db ;
  unsigned int n ;
  unsigned char *bits ;
  unsigned char *mark ;
  unsigned char mask ;
  unsigned char h : 1 ;
} ;

static void s6rc_graph_closure_rec (recinfo_t *recinfo, unsigned int i)
{
  if (!bitarray_peek(recinfo->mark, i))
  {
    unsigned int j = recinfo->db->services[i].ndeps[recinfo->h] ;
    bitarray_set(recinfo->mark, i) ;
    while (j--) s6rc_graph_closure_rec(recinfo, recinfo->db->deps[recinfo->h * recinfo->db->ndeps + recinfo->db->services[i].deps[recinfo->h] + j]) ;
    recinfo->bits[i] |= recinfo->mask ;
  }
}

void s6rc_graph_closure (s6rc_db_t const *db, unsigned char *bits, unsigned int bitno, int h)
{
  unsigned int n = db->nshort + db->nlong ;
  unsigned int m = bitarray_div8(n) ;
  unsigned char mark[m] ;
  recinfo_t info = { .db = db, .n = n, .bits = bits, .mark = mark, .mask = 1 << (bitno & 7), .h = !!h } ;
  unsigned int i = n ;
  memset(mark, 0, m) ;
  while (i--)
    if (bits[i] & info.mask) s6rc_graph_closure_rec(&info, i) ;
}
