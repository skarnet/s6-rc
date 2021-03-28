/* ISC license. */

#ifndef S6RCD_CLIENTRULES_H
#define S6RCD_CLIENTRULES_H

#include <stdint.h>


 /* Client rules */

extern void clientrules_init (unsigned int, char const *) ;
extern void clientrules_reload (void) ;
extern int clientrules_check (int, uint8_t *) ;

#endif
