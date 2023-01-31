/* ISC license. */

#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include <s6-rc/s6rc-utils.h>

#include <skalibs/posixishard.h>

int s6rc_livedir_prefixsize (char const *live, size_t *n)
{
  struct stat st ;
  size_t llen = strlen(live) ;
  char sfn[llen + 8] ;
  memcpy(sfn, live, llen) ;
  memcpy(sfn + llen, "/prefix", 8) ;
  if (stat(sfn, &st) < 0)
  {
    if (errno != ENOENT) return 0 ;
    *n = 0 ;
    return 1 ;
  }
  if (!S_ISREG(st.st_mode)) return (errno = EINVAL, 0) ;
  if (st.st_size > SKALIBS_PATH_MAX) return (errno = ENAMETOOLONG, 0) ;
  *n = st.st_size ;
  return 1 ;
}
