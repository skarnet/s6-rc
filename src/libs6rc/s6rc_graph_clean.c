/* ISC license. */

#include <stdint.h>

#include <skalibs/bitarray.h>

#include <s6-rc/s6rc-db.h>
#include <s6-rc/s6rc-utils.h>

typedef struct recinfo_s recinfo_t, *recinfo_t_ref ;
struct recinfo_s
{
  s6rc_db_t const *db ;
  unsigned char *bits ;
  unsigned char statemask ;
  unsigned char volmask ;
  unsigned char resmask ;
} ;

static void s6rc_graph_clean_rec (recinfo_t *info, uint32_t i)
{
  if (!(info->bits[i] & info->volmask))
  {
    uint32_t j = 0 ;
    for (; j < info->db->services[i].ndeps[0] ; j++)
      if (info->bits[j] & info->statemask && !(info->bits[j] & info->resmask)) break ;
    if (j >= info->db->services[i].ndeps[0])
    {
      info->bits[i] |= info->resmask ;
      for (j = 0 ; j < info->db->services[i].ndeps[1] ; j++)
        s6rc_graph_clean_rec(info, info->db->deps[info->db->ndeps + info->db->services[i].deps[1] + j]) ;
    }
  }
}

void s6rc_graph_clean (s6rc_db_t const *db, unsigned char *bits, unsigned int statebit, unsigned int volbit, unsigned int resbit)
{
  uint32_t n = db->nshort + db->nlong ;
  recinfo_t info = { .db = db, .bits = bits, .statemask = 1 << statebit, .volmask = 1 << volbit, .resmask = 1 << resbit } ;
  for (uint32_t i = 0 ; i < n ; i++) bits[i] &= ~info.resmask ;
  for (uint32_t i = 0 ; i < n ; i++) if (bits[i] & info.statemask) s6rc_graph_clean_rec(&info, i) ;
}
