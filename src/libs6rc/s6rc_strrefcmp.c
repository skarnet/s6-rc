/* ISC license. */

#include <string.h>

#include <s6-rc/s6rc-utils.h>

int s6rc_strrefcmp (void const *a, void const *b)
{
  return strcmp(*(char const *const *)a, *(char const *const *)b) ;
}
