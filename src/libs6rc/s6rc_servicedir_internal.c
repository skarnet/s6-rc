/* ISC license. */

#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <skalibs/posixplz.h>
#include <skalibs/types.h>
#include <skalibs/djbunix.h>
#include <s6/servicedir.h>

#include <s6-rc/s6rc-utils.h>
#include "s6rc-servicedir-internal.h"

static s6_servicedir_desc const svdir_file_list[] =
{
  { .name = "finish", .type = S6_FILETYPE_NORMAL, .options = S6_SVFILE_EXECUTABLE | S6_SVFILE_ATOMIC },
  { .name = "finish.user", .type = S6_FILETYPE_NORMAL, .options = S6_SVFILE_EXECUTABLE },
  { .name = "run", .type = S6_FILETYPE_NORMAL, .options = S6_SVFILE_EXECUTABLE | S6_SVFILE_MANDATORY | S6_SVFILE_ATOMIC },
  { .name = "run.user", .type = S6_FILETYPE_NORMAL, .options = S6_SVFILE_EXECUTABLE },
  { .name = "notification-fd", .type = S6_FILETYPE_UINT, .options = 0 },
  { .name = "lock-fd", .type = S6_FILETYPE_UINT, .options = 0 },
  { .name = "timeout-kill", .type = S6_FILETYPE_UINT, .options = 0 },
  { .name = "timeout-finish", .type = S6_FILETYPE_UINT, .options = 0 },
  { .name = "max-death-tally", .type = S6_FILETYPE_UINT, .options = 0 },
  { .name = "down-signal", .type = S6_FILETYPE_NORMAL, .options = 0 },
  { .name = "template", .type = S6_FILETYPE_DIR, .options = 0 },
  { .name = "data", .type = S6_FILETYPE_DIR, .options = 0 },
  { .name = "env", .type = S6_FILETYPE_DIR, .options = 0 },
  { .name = 0, .options = 0 }
} ;

s6_servicedir_desc const *const s6rc_servicedir_file_list = svdir_file_list ;

int s6rc_servicedir_copy_one (char const *src, char const *dst, s6_servicedir_desc const *p)
{
  size_t srclen = strlen(src) ;
  size_t dstlen = strlen(dst) ;
  size_t plen = strlen(p->name) ;
  char srcfn[srclen + plen + 2] ;
  char dstfn[dstlen + plen + 2] ;
  memcpy(srcfn, src, srclen) ;
  srcfn[srclen] = '/' ;
  memcpy(srcfn + srclen + 1, p->name, plen + 1) ;
  memcpy(dstfn, dst, dstlen) ;
  dstfn[dstlen] = '/' ;
  memcpy(dstfn + dstlen + 1, p->name, plen + 1) ;

  switch (p->type)
  {
    case S6_FILETYPE_NORMAL :
    {
      unsigned int mode = p->options & S6_SVFILE_EXECUTABLE ? 0755 : 0644 ;
      if (!(p->options & S6_SVFILE_ATOMIC ? filecopy_suffix(srcfn, dstfn, mode, ".new") : filecopy_unsafe(srcfn, dstfn, mode)))
      {
        if (errno != ENOENT || p->options & S6_SVFILE_MANDATORY) return 0 ;
      }
      break ;
    }
    case S6_FILETYPE_EMPTY :
      if (access(srcfn, F_OK) < 0)
      {
        if (errno != ENOENT || p->options & S6_SVFILE_MANDATORY) return 0 ;
      }
      else if (!touch(dstfn)) return 0 ;
      break ;
    case S6_FILETYPE_UINT :
    {
      unsigned int u ;
      int r = s6rc_read_uint(srcfn, &u) ;
      if (r < 0 || (!r && p->options & S6_SVFILE_MANDATORY)) return 0 ;
      if (r)
      {
        char fmt[UINT_FMT] ;
        r = uint_fmt(fmt, u) ;
        fmt[r++] = '\n' ;
        if (!openwritenclose_unsafe(dstfn, fmt, r)) return 0 ;
      }
      break ;
    }
    case S6_FILETYPE_DIR :
      if (!hiercopy(srcfn, dstfn))
      {
        if (errno != ENOENT || p->options & S6_SVFILE_MANDATORY) return 0 ;
      }
      if (!strcmp(p->name, "template"))
      {
        memcpy(dstfn + dstlen + 1, "instance", 9) ;
        if (mkdir(dstfn, 0755) == -1 && errno != EEXIST) return 0 ;
      }
      break ;
    default : return (errno = EDOM, 0) ;
  }
  return 1 ;
}
