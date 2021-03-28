/* ISC license. */

#ifndef S6RCD_COMMAND_H
#define S6RCD_COMMAND_H

#include <sys/uio.h>

#include <skalibs/textmessage.h>

 /* Client commands */

int command_handle (struct iovec const *, void *) ;

#endif
