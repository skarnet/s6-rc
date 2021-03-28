/* ISC license. */

#ifndef S6RCD_MAIN_H
#define S6RCD_MAIN_H


 /* Exported by the main file, s6-rcd.c */

typedef struct globalflags_s globalflags_t, *globalflags_t_ref ;
struct globalflags_s
{
  uint8_t lameduck : 1 ;
  uint8_t dbupdate : 1 ;
} ;

extern globalflags_t flags ;
extern tain_t lameduckdeadline ;
extern unsigned int verbosity ;

#endif
