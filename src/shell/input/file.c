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
 * Redirect stdin of tasks to a file.
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
};

static void file_input_destroy (struct file_input *fp)
{
    if (fp) {
        int saved_errno = errno;
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

    /*  Path will be opened separately in each task.
     *  Ensure access here though so users get a single error message
     *  before launching tasks.
     */
    if (access (fp->path, R_OK) < 0) {
        shell_die_errno (1, "error opening input file '%s'", fp->path);
        goto error;
    }
    return fp;
error:
    file_input_destroy (fp);
    return NULL;
}

static int file_input_task_exec (flux_plugin_t *p,
                                 const char *topic,
                                 flux_plugin_arg_t *args,
                                 void *data)
{
    struct file_input *fp = data;
    int fd;

    if ((fd = open (fp->path, O_RDONLY)) < 0) {
        fprintf (stderr,
                 "error opening input file '%s': %s",
                 fp->path,
                 strerror (errno));
        exit (1);
    }
    if (dup2 (fd, STDIN_FILENO) < 0) {
        fprintf (stderr, "dup2: %s", strerror (errno));
        exit (1);
    }
    return 0;
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

    if (flux_plugin_add_handler (p,
                                 "task.exec",
                                 file_input_task_exec,
                                 fp) < 0)
        return -1;

    return 0;
}

struct shell_builtin builtin_file_input = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = file_input_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
