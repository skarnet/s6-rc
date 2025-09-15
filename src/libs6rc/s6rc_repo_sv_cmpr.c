/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_sv_cmpr (void const *a, void const *b, void *aux)
{
  s6rc_repo_sv const *aa = a ;
  s6rc_repo_sv const *bb = b ;
  char const *s = aux ;
  return strcmp(s + aa->pos, s + bb->pos) ;
}
