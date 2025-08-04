/* ISC license. */

#include <stdint.h>

#include <skalibs/genalloc.h>

#include <s6-rc/s6rc-utils.h>

int s6rc_nlto0 (char *s, size_t start, size_t len, genalloc *indices)
{
  size_t origlen = genalloc_len(size_t, indices) ;
  size_t pos = start ;
  int wasnull = !indices->s ;
  int inword = 0 ;

  for (size_t i = start ; i < len ; i++)
  {
    if (s[i] == '\n')
    {
      s[i] = 0 ;
      if (!genalloc_append(size_t, indices, &pos)) goto err ;
      inword = 0 ;
    }
    else if (!inword) { pos = i ; inword = 1 ; }
  }
  if (inword) goto err ;
  return 1 ;

 err:
  if (wasnull) genalloc_free(size_t, indices) ;
  else genalloc_setlen(size_t, indices, origlen) ;
  return 0 ;
}
