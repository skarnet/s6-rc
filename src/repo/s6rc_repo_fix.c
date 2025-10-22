/* ISC license. */

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <skalibs/posixplz.h>
#include <skalibs/direntry.h>
#include <skalibs/strerr.h>
#include <skalibs/stralloc.h>
#include <skalibs/genalloc.h>

#include <s6-rc/repo.h>

int s6rc_repo_fix (char const *repo, uint32_t options, unsigned int verbosity)
{
  stralloc sa = STRALLOC_ZERO ;
  genalloc svlist = GENALLOC_ZERO ;  /* s6rc_repo_sv */
  genalloc badga = GENALLOC_ZERO ;  /* uint32_t */
  genalloc setind = GENALLOC_ZERO ;  /* size_t */
  int n ;

  if (verbosity >= 3)
    strerr_warni2x("checking set consistency in repository ", repo) ;

  n = s6rc_repo_list_sets(repo, &sa, &setind) ;
  if (n < 0)
  {
    strerr_warnfu1sys("list sets") ;
    return 0 ;
  }
  for (uint32_t i = 0 ; i < nsets ; i++)
  {
    if (verbosity >= 3)
      strerr_warni2x("checking set ", sa.s + genalloc_s(size_t, &setind)[i]) ;
    if (!s6rc_repo_fixset(repo, sa.s + genalloc_s(size_t, &setind)[i], options, verbosity, &sa, &svlist, &badga)) goto err ;
  }

  genalloc_free(s6rc_repo_sv, &svlist) ;
  genalloc_free(uint32_t, &badga) ;
  genalloc_free(size_t, &setind) ;
  stralloc_free(&sa) ;
  return 1 ;

 err:
  genalloc_free(s6rc_repo_sv, &svlist) ;
  genalloc_free(uint32_t, &badga) ;
  genalloc_free(size_t, &setind) ;
  stralloc_free(&sa) ;
  return 0 ;
}
