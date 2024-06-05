/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_CLIENT_H
#define _SUBPROCESS_CLIENT_H

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#include <flux/core.h>
#include "subprocess.h"

enum {
    SUBPROCESS_REXEC_STDOUT = 1,
    SUBPROCESS_REXEC_STDERR = 2,
    SUBPROCESS_REXEC_CHANNEL = 4,
};

flux_future_t *subprocess_rexec (flux_t *h,
                                 const char *service_name,
                                 uint32_t rank,
                                 flux_cmd_t *cmd,
                                 int flags);

int subprocess_rexec_get (flux_future_t *f);
bool subprocess_rexec_is_started (flux_future_t *f, pid_t *pid);
bool subprocess_rexec_is_stopped (flux_future_t *f);
bool subprocess_rexec_is_finished (flux_future_t *f, int *status);
bool subprocess_rexec_is_output (flux_future_t *f,
                                 const char **stream,
                                 const char **buf,
                                 int *len,
                                 bool *eof);

int subprocess_write (flux_future_t *f,
                      const char *stream,
                      const char *data,
                      int len,
                      bool eof);

flux_future_t *subprocess_kill (flux_t *h,
                                const char *service_name,
                                uint32_t rank,
                                pid_t pid,
                                int signum);


#endif /* !_SUBPROCESS_CLIENT_H */

// vi: ts=4 sw=4 expandtab
