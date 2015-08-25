/* ISC license. */

#ifndef S6RC_SERVICEDIR_INTERNAL_H
#define S6RC_SERVICEDIR_INTERNAL_H

typedef enum s6rc_filetype_e s6rc_filetype_t, *s6rc_filetype_t_ref ;
enum s6rc_filetype_e
{
  FILETYPE_NORMAL,
  FILETYPE_EMPTY,
  FILETYPE_UINT,
  FILETYPE_DIR
} ;

#define SVFILE_EXECUTABLE 0x01
#define SVFILE_MANDATORY 0x02
#define SVFILE_ATOMIC 0x04

typedef struct s6rc_servicedir_desc_s s6rc_servicedir_desc_t, *s6rc_servicedir_desc_t_ref ;
struct s6rc_servicedir_desc_s
{
  char const *name ;
  s6rc_filetype_t type ;
  unsigned char options ;
} ;

extern s6rc_servicedir_desc_t const *s6rc_servicedir_file_list ;
extern unsigned int const s6rc_servicedir_file_maxlen ;

extern int s6rc_servicedir_copy_one (char const *, char const *, s6rc_servicedir_desc_t const *) ;

#endif
