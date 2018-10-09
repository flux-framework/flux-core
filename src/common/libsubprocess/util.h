#ifndef _SUBPROCESS_UTIL_H
#define _SUBPROCESS_UTIL_H

#include "subprocess.h"

void init_pair_fds (int *fds);

void close_pair_fds (int *fds);

int cmd_option_bufsize (flux_subprocess_t *p, const char *name);

#endif /* !_SUBPROCESS_UTIL_H */
