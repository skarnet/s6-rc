/* ISC license. */

#include <string.h>
#include <errno.h>

#include <skalibs/direntry.h>
#include <skalibs/stralloc.h>

#include <s6/s6-supervise.h>

#include <s6-rc/s6rc-servicedir.h>

int s6rc_servicedir_manage (char const *live, char const *prefix, tain_t const *deadline, tain_t *stamp)
{
  stralloc names = STRALLOC_ZERO ;
  size_t livelen = strlen(live) ;
  size_t n = 0 ;
  DIR *dir ;
  char dirfn[livelen + 13] ;
  memcpy(dirfn, live, livelen) ;
  memcpy(dirfn + livelen, "/servicedirs", 13) ;
  dir = opendir(dirfn) ;
  if (!dir) return -1 ;
  for (;;)
  {
    direntry *d ;
    errno = 0 ;
    d = readdir(dir) ;
    if (!d) break ;
    if (d->d_name[0] == '.') continue ;
    if (!stralloc_catb(&names, dirfn, livelen + 12)
     || !stralloc_catb(&names, "/", 1)
     || !stralloc_cats(&names, d->d_name)
     || !stralloc_0(&names)) goto err ;
    n++ ;
  }
  if (errno) goto err ;
  dir_close(dir) ;
  if (!n) return 0 ;
  memcpy(dirfn + livelen + 1, "scandir", 8) ;
  {
    char const *p = names.s ;
    char const *servicedirs[n] ;
    int r ;
    for (size_t i = 0 ; i < n ; p += strlen(p) + 1) servicedirs[i++] = p ;
    r = s6_supervise_link(dirfn, servicedirs, n, prefix, 0, deadline, stamp) ;
    stralloc_free(&names) ;
    return r ;
  }

 err:
  dir_close(dir) ;
  stralloc_free(&names) ;
  return -1 ;
}
