/* ISC license. */

#include <skalibs/bsdsnowflake.h> /* stat.h */
#include <skalibs/nonposix.h>  /* mkdtemp */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <skalibs/stat.h>
#include <skalibs/stralloc.h>
#include <skalibs/djbunix.h>

#include <s6-rc/s6rc-utils.h>

int s6rc_livedir_create (stralloc *sa, char const *live, char const *suffix, char const *scdir, char const *prefix, char const *compiled, unsigned char const *state, unsigned int statelen, size_t *dirlen)
{
  size_t newlen, ddirlen ;
  size_t sabase = sa->len ;
  mode_t m ;
  int wasnull = !sa->s ;
  if (!s6rc_sanitize_dir(sa, live, &ddirlen)) return 0 ;
  if (!stralloc_cats(sa, ":")) goto err ;
  if (!stralloc_cats(sa, suffix)) goto err ;
  if (!stralloc_cats(sa, ":XXXXXX")) goto err ;
  if (!stralloc_0(sa)) goto err ;
  m = umask(0) ;
  if (!mkdtemp(sa->s + sabase)) { umask(m) ; goto err ; }
  newlen = sa->len-- ;
  if (!stralloc_catb(sa, "/servicedirs", 13)) { umask(m) ; goto delerr ; }  /* allocates enough for the next strcpys */
  if (mkdir(sa->s + sabase, 0755) == -1) { umask(m) ; goto delerr ; }
  umask(m) ;
  if (chmod(sa->s + sabase, 0755) == -1) goto delerr ;
  strcpy(sa->s + newlen, "compiled") ;
  if (symlink(compiled, sa->s + sabase) == -1) goto delerr ;
  strcpy(sa->s + newlen, "scandir") ;
  if (symlink(scdir, sa->s + sabase) == -1) goto delerr ;
  strcpy(sa->s + newlen, "prefix") ;
  if (!openwritenclose_unsafe(sa->s + sabase, prefix, strlen(prefix))) goto delerr ;
  strcpy(sa->s + newlen, "state") ;
  if (!openwritenclose_unsafe(sa->s + sabase, (char const *)state, statelen)) goto delerr ;
  strcpy(sa->s + newlen, "lock") ;
  if (!openwritenclose_unsafe(sa->s + sabase, "", 0)) goto delerr ;
  sa->len = newlen-1 ;
  sa->s[newlen-1] = 0 ;
  if (chmod(sa->s + sabase, 0755) == -1) goto delerr ;
  *dirlen = ddirlen ;
  return 1 ;

 delerr:
  {
    int e = errno ;
    sa->s[newlen-1] = 0 ;
    rm_rf_in_tmp(sa, sabase) ;
    errno = e ;
  }
 err:
  if (wasnull) stralloc_free(sa) ;
  else sa->len = sabase ;
  return 0 ;
}
