/* ISC license. */

#include <stdint.h>
#include <sys/uio.h>
#include <errno.h>

#include "db.h"
#include "state.h"

int deps_fulfilled (s6rc_db_t const *db, mstate_t const *m, s6rc_id_t id, char const *param, int h)
{
  s6rc_common_t const *common = s6rc_service_common(db, id) ;
  for (uint32_t i = 0 ; i < common->ndeps[h] ; i++)
  {
    s6rc_id_t depid = db->deps[h][common->deps[h] + i] ;
    sstate_t *st = sstate(m, depid, param) ;
    uint8_t deptype = db->deptypes[h][common->deps[h] + i] ;
    if (!(st->bits & SSTATE_WANTED) == h && stype(depid) != S6RC_STYPE_EXTERNAL && stype(depid) != S6RC_STYPE_N + S6RC_STYPE_EXTERNAL) return 0 ;
    if (!(st->bits & SSTATE_CURRENT) != h && !(st->bits & SSTATE_TRANSITIONING)) continue ;
    if (s6rc_deptype_soft(deptype) && st->bits & SSTATE_FAILED) continue ;
    if (s6rc_deptype_loose(deptype) && !(st->bits & SSTATE_TRANSITIONING)) continue ;
    return 0 ;
  }
  return 1 ;
}

static int sstate_zerotmp (sstate_t *st, void *arg)
{
  st->tmp = 0 ;
  (void)arg ;
  return 1 ;
}

static int instance_zerotmp (instance_t *ins, void *arg)
{
  return sstate_zerotmp(&ins->sstate) ;
}

typedef struct recinfo_s recinfo_t, *recinfo_t_ref ;
struct recinfo_s
{
  s6rc_db_t const *db ;
  mstate_t *m ;
  char const *param ;
  uint8_t h : 1 ;
}

static int mstate_dep_closure_rec (recinfo_t *recinfo, s6rc_id_t id)
{
  sstate_t *st = sstate(recinfo->db, recinfo->m, id, recinfo->param) ;
  if (!st)
  {
    st = instance_create(recinfo->m, id, recinfo->param) ;
    if (!st) return 0 ;
  }
  if (!(st->tmp & SSTATE_GRAY))
  {
    uint32_t ndeps = re
  }
}

static void mstate_dep_closure (s6rc_db_t const *db, mstate_t *m, sstate_t *st, char const *param, int h)
{
  recinfo_t recinfo = { .db = db, .m = m, .param = param, .h = !!h } ;
  mstate_dep_closure_rec(&recinfo, st) ;
}

int mstate_change_wanted (s6rc_db_t const *db, cdb_t *c, mstate_t *m, char const *const *args, size_t n, int h)
{
  mstate_iterate(m, db->n, &sstate_zerotmp, &instancelen_nop, &instance_zerotmp, 0) ;
  for (size_t i = 0 ; i < n ; n++)
  {
    sstate_t *st ;
    s6rc_id_t id ;
    char const *param ;
    int r = s6rc_service_resolve(c, args[i], &id, &param) ;
    if (r < 0) return -1 ;
    if (!r) return 1 + i ;
    st = sstate(db, m, id, param) ;
    if (!st)  /* instance not found */
    {
      st = instance_create(m, id, param) ;
      if (!st) return -1 ;
    }
    st->tmp |= SSTATE_MARK | SSTATE_EXPLICIT ;
    mstate_dep_closure(db, m, st, param, h) ;
  }
}
