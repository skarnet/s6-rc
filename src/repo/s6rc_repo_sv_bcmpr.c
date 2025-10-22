/* ISC license. */

#include <string.h>

#include <s6-rc/repo.h>

int s6rc_repo_sv_bcmpr (void const *a, void const *b, void *aux)
{
  char const *key = a ;
  s6rc_repo_sv const *elem = b ;
  char const *s = aux ;
  return strcmp(key, s + elem->pos) ;
}
