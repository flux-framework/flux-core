/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* file input handling
 *
 * For input type "file", read a single input file on shell rank 0
 * and place data in guest.input.
 *
 */
#define FLUX_SHELL_PLUGIN_NAME "input.file"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "task.h"
#include "internal.h"
#include "builtins.h"
#include "input/util.h"

struct file_input {
    flux_shell_t *shell;
    const char *path;
    int fd;
    flux_watcher_t *w;
    char *ranks;
};

static void file_read_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg);

static void file_input_destroy (struct file_input *fp)
{
    if (fp) {
        int saved_errno = errno;
        if (fp->fd >= 0)
            close (fp->fd);
        flux_watcher_destroy (fp->w);
        free (fp->ranks);
        free (fp);
        errno = saved_errno;
    }
}

static struct file_input *file_input_create (flux_shell_t *shell,
                                             const char *path)
{
    struct file_input *fp;

    if (!(fp = calloc (1, sizeof (*fp))))
        return NULL;

    fp->shell = shell;
    fp->path = path;

    if ((fp->fd = open (fp->path, O_RDONLY)) < 0) {
        shell_die_errno (1, "error opening input file '%s'", fp->path);
        goto error;
    }
    if (!(fp->w = flux_fd_watcher_create (shell->r,
                                          fp->fd,
                                          FLUX_POLLIN,
                                          file_read_cb,
                                          fp))) {
        shell_log_errno ("flux_fd_watcher_create");
        goto error;
    }
    if (shell->info->total_ntasks > 1) {
        if (asprintf (&fp->ranks, "[0-%d]", shell->info->total_ntasks) < 0) {
            shell_log_errno ("asprintf");
            goto error;
        }
    }
    else {
        if (!(fp->ranks = strdup ("0"))) {
            shell_log_errno ("asprintf");
            goto error;
        }
    }
    return fp;
error:
    file_input_destroy (fp);
    return NULL;
}

static int file_input_to_kvs (struct file_input *fp,
                              void *buf,
                              int len,
                              bool eof)
{
    json_t *context = NULL;
    int saved_errno;
    int rc = -1;

    if (!(context = ioencode ("stdin", fp->ranks, buf, len, eof)))
        goto error;
    if (input_eventlog_put (fp->shell, context) < 0)
        goto error;
    rc = 0;
 error:
    saved_errno = errno;
    json_decref (context);
    errno = saved_errno;
    return rc;
}

static void file_read_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    struct file_input *fp = arg;
    long ps = sysconf (_SC_PAGESIZE);
    char buf[ps];
    ssize_t n;

    assert (ps > 0);

    while ((n = read (fp->fd, buf, ps)) > 0) {
        if (file_input_to_kvs (fp, buf, n, false) < 0)
            shell_die_errno (1, "shell_input_put_kvs_raw");
    }
    if (n < 0)
        shell_die_errno (1, "shell_input_put_kvs_raw");
    if (file_input_to_kvs (fp, NULL, 0, true) < 0)
        shell_die_errno (1, "shell_input_put_kvs_raw");

    flux_watcher_stop (w);
}

static int file_input_init (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    const char *type = NULL;
    const char *path = NULL;
    struct file_input *fp;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!shell)
        return -1;
    if (flux_shell_getopt_unpack (shell,
                                  "input",
                                  "{s?{s?s s?s}}",
                                  "stdin",
                                   "type", &type,
                                   "path", &path) < 0)
        return -1;

    if (!type || !streq (type, "file"))
        return 0;

    if (path == NULL) {
        shell_log_error ("path for stdin file input not specified");
        return -1;
    }
    if (!(fp = file_input_create (shell, path))
        || flux_plugin_aux_set (p,
                                NULL,
                                fp,
                                (flux_free_f) file_input_destroy) < 0) {
        shell_log_error ("file input creation failed");
        file_input_destroy (fp);
        return -1;
    }

    /* Emit input eventlog header */
    if (input_eventlog_init (shell) < 0)
        return -1;

    /* Eventlog is ready, start fd watcher */
    flux_watcher_start (fp->w);

    return 0;
}

struct shell_builtin builtin_file_input = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = file_input_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
