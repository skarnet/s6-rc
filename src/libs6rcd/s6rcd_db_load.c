/* ISC license. */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <s6-rc/db.h>
#include "s6rcd.h"

int s6rcd_db_load (s6rc_db_t *db, char const *compiled)
{
  if (!s6rcd_db_read_sizes(db, compiled)) return 0 ;
  
}
