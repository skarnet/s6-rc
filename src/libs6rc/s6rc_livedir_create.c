/* ISC license. */

#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>  /* mkdtemp */
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>

int s6rc_livedir_create (stralloc *sa, char const *live, char const *suffix, char const *scandir, char const *prefix, char const *compiled, unsigned char const *state, unsigned int statelen, size_t *dirlen)
{
  size_t newlen, ddirlen ;
  size_t sabase = sa->len ;
  int wasnull = !sa->s ;
  if (!s6rc_sanitize_dir(sa, live, &ddirlen)) return 0 ;
  if (!stralloc_cats(sa, ":")) goto err ;
  if (!stralloc_cats(sa, suffix)) goto err ;
  if (!stralloc_cats(sa, ":XXXXXX")) goto err ;
  if (!stralloc_0(sa)) goto err ;
  if (!mkdtemp(sa->s + sabase)) goto err ;
  newlen = sa->len-- ;
  if (chmod(sa->s + sabase, 0755) < 0) goto delerr ;
  if (!stralloc_catb(sa, "/servicedirs", 13)) goto delerr ;  /* allocates enough for the next strcpys */
  if (mkdir(sa->s + sabase, 0755) < 0) goto delerr ;
  strcpy(sa->s + newlen, "compiled") ;
  if (symlink(compiled, sa->s + sabase) < 0) goto delerr ;
  strcpy(sa->s + newlen, "scandir") ;
  if (symlink(scandir, sa->s + sabase) < 0) goto delerr ;
  strcpy(sa->s + newlen, "prefix") ;
  if (!openwritenclose_unsafe(sa->s + sabase, prefix, strlen(prefix))) goto delerr ;
  strcpy(sa->s + newlen, "state") ;
  if (!openwritenclose_unsafe(sa->s + sabase, (char const *)state, statelen)) goto delerr ;
  sa->len = newlen-1 ;
  sa->s[newlen-1] = 0 ;
  *dirlen = ddirlen ;
  return 1 ;

 delerr:
  {
    int e = errno ;
    sa->s[newlen] = 0 ;
    rm_rf_in_tmp(sa, sabase) ;
    errno = e ;
  }
 err:
  if (wasnull) stralloc_free(sa) ;
  else sa->len = sabase ;
  return 0 ;
}
