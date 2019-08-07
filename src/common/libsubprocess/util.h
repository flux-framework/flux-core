/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_UTIL_H
#define _SUBPROCESS_UTIL_H

#include "subprocess.h"

void init_pair_fds (int *fds);

void close_pair_fds (int *fds);

int cmd_option_bufsize (flux_subprocess_t *p, const char *name);

int cmd_option_line_buffer (flux_subprocess_t *p, const char *name);

int cmd_option_stream_stop (flux_subprocess_t *p, const char *name);

#endif /* !_SUBPROCESS_UTIL_H */
