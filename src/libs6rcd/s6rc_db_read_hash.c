/* ISC license. */

#include <string.h>
#include <skalibs/djbunix.h>

#include "db.h"

int s6rc_db_read_hash (char const *dir, char *dbhash)
{
  size_t len = strlen(dir) ;
  char fn[len + 6] ;
  memcpy(fn, dir, len) ;
  memcpy(fn + len, "/hash", 6) ;
  return openreadnclose(fn, dbhash, 32) == 32 ;
}
