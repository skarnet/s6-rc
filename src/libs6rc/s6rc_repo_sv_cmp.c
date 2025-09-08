/* ISC license. */

#include <s6-rc/repo.h>

int s6rc_repo_sv_cmp (void const *a, void const *b, void *aux)
{
  s6rc_repo_sv const *aa = a ;
  s6rc_repo_sv const *bb = b ;
  stralloc *sa = aux ;
  return strcmp(sa->s + aa->pos, sa->s + bb->pos) ;
}
