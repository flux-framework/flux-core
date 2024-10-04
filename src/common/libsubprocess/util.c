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
# include "config.h"
#endif

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/parse_size.h"

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

    if (asprintf (&var, "%s_BUFSIZE", name) < 0)
        goto cleanup;

    if ((val = flux_cmd_getopt (p->cmd, var))) {
        uint64_t size;
        if (parse_size (val, &size) < 0)
            goto cleanup;
        if (size == 0) {
            errno = EINVAL;
            goto cleanup;
        }
        if (size > INT_MAX) {
            errno = EOVERFLOW;
            goto cleanup;
        }
        rv = (int) size;
    }
    else
        rv = SUBPROCESS_DEFAULT_BUFSIZE;

cleanup:
    free (var);
    return rv;
}

int cmd_option_line_buffer (flux_subprocess_t *p, const char *name)
{
    char *var;
    const char *val;
    int rv = -1;

    if (asprintf (&var, "%s_LINE_BUFFER", name) < 0)
        goto cleanup;

    if ((val = flux_cmd_getopt (p->cmd, var))) {
        if (!strcasecmp (val, "false"))
            rv = 0;
        else if (!strcasecmp (val, "true"))
            rv = FBUF_WATCHER_LINE_BUFFER;
        else
            errno = EINVAL;
    }
    else
        rv = FBUF_WATCHER_LINE_BUFFER;

cleanup:
    free (var);
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
