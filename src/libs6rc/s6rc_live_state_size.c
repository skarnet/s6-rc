/* ISC license. */

#include <stdint.h>
#include <string.h>

#include <skalibs/djbunix.h>

#include <s6-rc/s6rc-db.h>
#include <s6-rc/s6rc-utils.h>

int s6rc_live_state_size (char const *live, uint32_t *nlong, uint32_t *nshort)
{
  s6rc_db_t db ;
  int fd ;
  size_t livelen = strlen(live) ;
  char fn[livelen + 10] ;
  memcpy(fn, live, livelen) ;
  memcpy(fn + livelen, "/compiled", 10) ;
  fd = openc_readb(fn) ;
  if (fd == -1) return 0 ;
  if (!s6rc_db_read_sizes(fd, &db))
  {
    fd_close(fd) ;
    return 0 ;
  }
  fd_close(fd) ;
  *nlong = db.nlong ;
  *nshort = db.nshort ;
  return 1 ;
}
