/* ISC license. */

#ifndef S6_RC_COMPILE_H
#define S6_RC_COMPILE_H

#include <execline/config.h>
#include <s6/config.h>

#define S6RC_ONESHOT_RUNNER_RUNSCRIPT \
"#!" EXECLINE_EXTBINPREFIX "execlineb -P\n" \
EXECLINE_EXTBINPREFIX "fdmove -c 2 1\n" \
S6_EXTBINPREFIX "s6-ipcserver-socketbinder -- s\n" \
S6_EXTBINPREFIX "s6-notifywhenup -f --\n" \
S6_EXTBINPREFIX "s6-ipcserverd -1 --\n" \
S6_EXTBINPREFIX "s6-ipcserver-access -E -l0 -i data/rules --\n" \
S6_EXTBINPREFIX "s6-sudod -t 2000 --\n"

#endif

