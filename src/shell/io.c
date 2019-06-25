/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* stdio handling
 *
 * Intercept task stdout, stderr and dispose of it according to
 * selected I/O mode.
 *
 * N.B. for the moment, emit on shell's stdout, stderr with task labels.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <flux/core.h>

#include "task.h"
#include "info.h"
#include "io.h"

struct shell_io {
    flux_t *h;
    struct shell_info *info;
};

void shell_io_destroy (struct shell_io *io)
{
    if (io) {
        int saved_errno = errno;
        free (io);
        errno = saved_errno;
    }
}

struct shell_io *shell_io_create (flux_t *h, struct shell_info *info)
{
    struct shell_io *io;

    if (!(io = calloc (1, sizeof (*io))))
        return NULL;
    io->h = h;
    io->info = info;

    return io;
}

// shell_task_io_ready_f callback footprint
void shell_io_task_ready (struct shell_task *task, const char *name, void *arg)
{
    //struct shell_io *io = arg;

    const char *line;
    int len;

    len = shell_task_io_readline (task, name, &line);
    if (len > 0) {
        FILE *f = !strcmp (name, "STDOUT") ? stdout : stderr;
        fprintf (f, "%d: ", task->rank);
        fwrite (line, len, 1, f);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
