/* ISC license. */

#include <stdint.h>
#include <sys/uio.h>
#include <errno.h>

#include <s6-rc/db.h>
#include "state.h"

static int state_allownext (sstate_t const *st, uint8_t deptype, uint8_t subtype, int h)
{
  return !(st->bits & SSTATE_WANTED) == h && subtype != S6RC_STYPE_EXTERNAL && subtype != S6RC_STYPE_N + S6RC_STYPE_EXTERNAL ? 0 :
  (!(st->bits & SSTATE_CURRENT) != h && !(st->bits & SSTATE_TRANSITIONING))
   || (s6rc_deptype_soft(deptype) && st->bits & SSTATE_FAILED)
   || (s6rc_deptype_loose(deptype) && !(st->bits & SSTATE_TRANSITIONING))
}

static int instances_testalldown (mstate_t const *m, uint8_t deptype, uint8_t type, uint32_t num)
{
  instance_t const *ins = genalloc_s(instance_t, m->dyn[type] + num) ;
  size_t n = genalloc_len(instance_t, m->dyn[type] + num) ;
  for (size_t i = 0 ; i < n ; i++)
    if (!state_allownext(&ins[i].sstate, deptype, subtype, 0)) return 0 ;
  return 1 ;
}

int deps_fulfilled (s6rc_db_t const *db, mstate_t const *m, uint32_t id, char const *param, int h)
{
  s6rc_common_t const *common ;
  uint32_t num ;
  uint8_t type ;
  s6rc_service_typenum(db->n, id, &type, &num) ;
  common = s6rc_service_common_tn(db, type, num) ;
  for (uint32_t i = 0 ; i < common->ndeps[h] ; i++)
  {
    uint32_t subnum ;
    uint8_t subtype ;
    uint8_t deptype = db->deptypes[h][common->deps[h] + i] ;
    s6rc_service_typenum(db->n, db->deps[h][common->deps[h] + i], &subtype, &subnum) ;
    if (!h && type < S6RC_STYPE_N && subtype >= S6RC_STYPE_N)
      return instances_testalldown(m, deptype, subtype, subnum) ;
    if (h && type >= S6RC_STYPE_N && subtype < S6RC_STYPE_N)
      param = 0 ;
    if (!state_allownext(sstate_tn(m, subtype, subnum, param), deptype, subtype, h))
      return 0 ;
  }
  return 1 ;
}

typedef struct recinfo_s recinfo_t, *recinfo_t_ref ;
struct recinfo_s
{
  s6rc_db_t const *db ;
  mstate_t *m ;
  char const *param ;
  uint8_t h : 1 ;
  uint8_t force : 1 ;
}

static int mstate_dep_closure_rec (recinfo_t *recinfo, uint32_t id, char const *param)
{
  sstate_t *st ;
  st = sstate(recinfo->db, recinfo->m, id, param) ;
  if (!st)
  {
    if (!recinfo->h) return 1 ;
    st = instance_create(recinfo->m, id, param) ;
    if (!st) return 0 ;
  }
  if (!(st->tmp & SSTATE_TRANSITIONING))
  {
    s6rc_common_t const *common ;
    uint32_t num ;
    uint8_t type ;
    st->tmp |= SSTATE_TRANSITIONING ;
    s6rc_service_typenum(recinfo->db->n, id, &type, &num) ;
    common = s6rc_service_common_tn(recinfo->db, type, num) ;
    if (!recinfo->h && !recinfo->force && common->flags & S6RC_DB_FLAG_ESSENTIAL) return (errno = EPERM, 0) ;
    for (uint32_t i = 0 ; i < common->ndeps[recinfo->h] ; i++)
    {
      uint32_t subid = recinfo->db->deps[recinfo->h][common->deps[recinfo->h] + i] ;
      uint32_t subnum ;
      uint8_t subtype ;
      if (s6rc_service_recheck_instance(recinfo->db, &subid, &param) < 0) return (errno = EPROTO, 0) ;
      if (deptype_passive(recinfo->db->deptypes[recinfo->h][common->deps[recinfo->h] + i])) continue ;
      s6rc_service_typenum(recinfo->db->n, subid, &subtype, &subnum) ;
      if (!recinfo->h && type < S6RC_STYPE_N && subtype >= S6RC_STYPE_N)
      {
        instance_t const *ins = genalloc_s(instance_t, recinfo->m->dyn[subtype] + subnum) ;
        size_t n = genalloc_len(instance_t, recinfo->m->dyn[subtype] + subnum) ;
        for (size_t j = 0 ; j < n ; j++)
          if (!mstate_dep_closure_rec(recinfo, subid, ins[j].param)) return 0 ;
      }
      else
      {
        if (recinfo->h && type >= S6RC_STYPE_N && subtype < S6RC_STYPE_N)
          param = 0 ;
        if (!mstate_dep_closure_rec(recinfo, subid, param))
          return 0 ;
      }
    }
    st->tmp |= SSTATE_WANTED ;
  }
}

static void mstate_dep_closure (s6rc_db_t const *db, mstate_t *m, sstate_t *st, char const *param, int h, int force)
{
  recinfo_t recinfo = { .db = db, .m = m, .param = param, .h = !!h, .force = !!force } ;
  mstate_dep_closure_rec(&recinfo, st) ;
}

int mstate_change_wanted (s6rc_db_t const *db, mstate_t *m, char const *const *args, size_t n, int h, int force)
{
  mstate_zerotmp(m, db->n) ;
  for (size_t i = 0 ; i < n ; n++)
  {
    sstate_t *st ;
    uint32_t id ;
    char const *param ;
    int r = s6rc_service_resolve(&db->resolve, args[i], &id, &param) ;
    if (r < 0) return -1 ;
    if (!r) return 1 + i ;
    st = sstate(db, m, id, param) ;
    if (!st)  /* instance not found */
    {
      st = instance_create(m, id, param) ;
      if (!st) return -1 ;
    }
    st->tmp |= SSTATE_WANTED | SSTATE_EXPLICIT ;
    mstate_dep_closure(db, m, st, param, h) ;
  }
}
