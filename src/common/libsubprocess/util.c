/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif

#include <sys/types.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>

#include <czmq.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "util.h"

void init_pair_fds (int *fds)
{
    fds[0] = -1;
    fds[1] = -1;
}

void close_pair_fds (int *fds)
{
    if (!fds)
        return;
    if (fds[0] != -1)
        close (fds[0]);
    if (fds[1] != -1)
        close (fds[1]);
}

int cmd_option_bufsize (flux_subprocess_t *p, const char *name)
{
    char *var;
    const char *val;
    int rv = -1;

    if (asprintf (&var, "%s_BUFSIZE", name) < 0) {
        log_err ("asprintf");
        goto cleanup;
    }

    if ((val = flux_cmd_getopt (p->cmd, var))) {
        char *endptr;
        errno = 0;
        rv = strtol (val, &endptr, 10);
        if (errno || endptr[0] != '\0' || rv <= 0) {
            rv = -1;
            errno = EINVAL;
            goto cleanup;
        }
    } else
        rv = SUBPROCESS_DEFAULT_BUFSIZE;

cleanup:
    free (var);
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
