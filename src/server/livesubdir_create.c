/* ISC license. */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <skalibs/djbunix.h>

#include "s6rcd.h"

int s6rcd_livesubdir_create (char *name, char const *live, char const *compiled)
{
  size_t llen = strlen(live) ;
  size_t clen = strlen(compiled) ;
  char cfn[clen + 13] ;
  char lfn[llen + 25] ;
  memcpy(lfn, live, llen) ;
  memcpy(lfn + llen, "/live:XXXXXX", 13) ;
  if (!mkdtemp(lfn)) return 0 ;

  memcpy(lfn + llen + 12, "/compiled", 10) ;
  if (symlink(compiled, lfn) < 0) return 0 ;
    strerr_diefu4sys(111, "symlink ", compiled, " to ", realfn + llen + 1) ;

  memcpy(cfn, compiled, clen) ;
  memcpy(cfn + clen, "/servicedirs", 13) ;
  memcpy(lfn + llen + 13, "servicedirs", 12) ;
  if (!hiercopy(cfn, lfn)) return 0 ;

  lfn[llen + 12] = 0 ;
  if (chmod(lfn, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0) return 0 ;
  memcpy(name, lfn + llen + 1, 12) ;
  return 1 ;
}
