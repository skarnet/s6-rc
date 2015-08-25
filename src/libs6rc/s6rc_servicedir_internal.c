/* ISC license. */

#include <errno.h>
#include <unistd.h>
#include <skalibs/uint.h>
#include <skalibs/bytestr.h>
#include <skalibs/djbunix.h>
#include <s6-rc/s6rc-utils.h>
#include "s6rc-servicedir-internal.h"

static s6rc_servicedir_desc_t const svdir_file_list[] =
{
  { .name = "finish", .type = FILETYPE_NORMAL, .options = SVFILE_EXECUTABLE | SVFILE_ATOMIC },
  { .name = "finish.user", .type = FILETYPE_NORMAL, .options = SVFILE_EXECUTABLE },
  { .name = "run", .type = FILETYPE_NORMAL, .options = SVFILE_EXECUTABLE | SVFILE_MANDATORY | SVFILE_ATOMIC },
  { .name = "run.user", .type = FILETYPE_NORMAL, .options = SVFILE_EXECUTABLE },
  { .name = "nosetsid", .type = FILETYPE_EMPTY, .options = 0 },
  { .name = "notification-fd", .type = FILETYPE_UINT, .options = 0 },
  { .name = "timeout-finish", .type = FILETYPE_UINT, .options = 0 },
  { .name = "data", .type = FILETYPE_DIR, .options = 0 },
  { .name = "env", .type = FILETYPE_DIR, .options = 0 },
  { .name = 0, .options = 0 }
} ;

s6rc_servicedir_desc_t const *s6rc_servicedir_file_list = svdir_file_list ;
unsigned int const s6rc_servicedir_file_maxlen = 15 ;

int s6rc_servicedir_copy_one (char const *src, char const *dst, s6rc_servicedir_desc_t const *p)
{
  unsigned int srclen = str_len(src) ;
  unsigned int dstlen = str_len(dst) ;
  unsigned int plen = str_len(p->name) ;
  char srcfn[srclen + plen + 2] ;
  char dstfn[dstlen + plen + 2] ;
  byte_copy(srcfn, srclen, src) ;
  srcfn[srclen] = '/' ;
  byte_copy(srcfn + srclen + 1, plen + 1, p->name) ;
  byte_copy(dstfn, dstlen, dst) ;
  dstfn[dstlen] = '/' ;
  byte_copy(dstfn + dstlen + 1, plen + 1, p->name) ;

  switch (p->type)
  {
    case FILETYPE_NORMAL :
    {
      unsigned int mode = p->options & SVFILE_EXECUTABLE ? 0755 : 0644 ;
      if (!(p->options & SVFILE_ATOMIC ? filecopy_suffix(srcfn, dstfn, mode, ".new") : filecopy_unsafe(srcfn, dstfn, mode)))
      {
        if (errno != ENOENT || p->options & SVFILE_MANDATORY) return 0 ;
      }
      break ;
    }
    case FILETYPE_EMPTY :
      if (access(srcfn, F_OK) < 0)
      {
        if (errno != ENOENT || p->options & SVFILE_MANDATORY) return 0 ;
      }
      else if (!touch(dstfn)) return 0 ;
      break ;
    case FILETYPE_UINT :
    {
      unsigned int u ;
      register int r = s6rc_read_uint(srcfn, &u) ;
      if (r < 0 || (!r && p->options & SVFILE_MANDATORY)) return 0 ;
      if (r)
      {
        char fmt[UINT_FMT] ;
        r = uint_fmt(fmt, u) ;
        fmt[r++] = '\n' ;
        if (!openwritenclose_unsafe(dstfn, fmt, r)) return 0 ;
      }
      break ;
    }
    case FILETYPE_DIR :
      if (!hiercopy(srcfn, dstfn))
      {
        if (errno != ENOENT || p->options & SVFILE_MANDATORY) return 0 ;
      }
      break ;
    default : return (errno = EDOM, 0) ;
  }
  return 1 ;
}
