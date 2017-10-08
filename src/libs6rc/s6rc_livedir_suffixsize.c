/* ISC license. */

#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <s6-rc/s6rc-utils.h>

int s6rc_livedir_suffixsize (char const *live, size_t *n)
{
  struct stat st ;
  size_t llen = strlen(live) ;
  char sfn[llen + 8] ;
  memcpy(sfn, live, llen) ;
  memcpy(sfn + llen, "/suffix", 8) ;
  if (stat(sfn, &st) < 0)
  {
    if (errno != ENOENT) return 0 ;
    *n = 0 ;
    return 1 ;
  }
  if (!S_ISREG(st.st_mode)) return (errno = EINVAL, 0) ;
  *n = st.st_size ;
  return 1 ;
}
