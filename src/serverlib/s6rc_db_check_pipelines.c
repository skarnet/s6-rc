/* ISC license. */

#include <string.h>
#include <stdint.h>
#include <skalibs/diuint32.h>
#include <s6-rc/s6rc-db.h>

struct recinfo_s
{
  s6rc_db_t const *db ;
  unsigned char *mark ;
} ;

static uint32_t check_prod_rec (struct recinfo_s *recinfo, uint32_t n)
{
  uint32_t i = 0 ;
  if (recinfo->mark[n] & 3) return n ;
  recinfo->mark[n] |= 1 ;
  for (; i < recinfo->db->services[n].x.longrun.nproducers ; i++)
  {
    uint32_t j = recinfo->db->producers[recinfo->db->services[n].x.longrun.producers + i] ;
    if (j >= recinfo->db->nlong) return (recinfo->mark[n] |= 4, n) ;
    if (recinfo->db->services[j].x.longrun.consumer != n) return (recinfo->mark[j] |= 4, j) ;
    j = check_prod_rec(recinfo, j) ;
    if (j < recinfo->db->nlong) return j ;
  }
  recinfo->mark[n] |= 2 ;
  return recinfo->db->nlong ;
}

int s6rc_db_check_pipelines (s6rc_db_t const *db, diuint32 *problem)
{
  uint32_t i = db->nlong ;
  unsigned char mark[db->nlong] ;
  struct recinfo_s recinfo = { .db = db, .mark = mark } ;
  memset(mark, 0, db->nlong) ;
  while (i--)
  {
    if (db->services[i].x.longrun.consumer >= db->nlong && db->services[i].x.longrun.nproducers)
    {
      uint32_t j = check_prod_rec(&recinfo, i) ;
      if (j < db->nlong)
      {
        problem->left = i ;
        problem->right = j ;
        return mark[j] & 4 ? 3 : mark[j] & 2 ? 2 : 1 ;
      }
    }
  }
  i = db->nlong ;
  while (i--)
  {
    if (!mark[i] && db->services[i].x.longrun.nproducers)
    {
      problem->left = db->services[i].x.longrun.consumer ;
      problem->right = i ;
      return 1 ;
    }
  }
  return 0 ;
}
