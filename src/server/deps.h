/* ISC license. */

#ifndef S6RCD_DEPS_H
#define S6RCD_DEPS_H

#include <stdint.h>

#include "db.h"


 /* Dependencies */

#define deptype_passive(dt) ((dt) & 0x01u)
#define deptype_soft(dt) ((dt) & 0x02u)
#define deptype_loose(dt) ((dt) & 0x04u)

extern int deps_fulfilled (s6rc_db_t const *, mstate_t const *, uint32_t, char const *, int) ;

#endif
