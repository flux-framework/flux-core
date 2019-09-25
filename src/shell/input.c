/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* std input handling
 *
 * Currently, only standard input via file is supported.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

#include "task.h"
#include "internal.h"
#include "builtins.h"

enum {
    FLUX_INPUT_TYPE_NONE = 1,
    FLUX_INPUT_TYPE_FILE = 2,
};

struct shell_input_type_file {
    const char *path;
    int *fds;
    int count;
};

struct shell_input {
    flux_shell_t *shell;
    int stdin_type;
    struct shell_input_type_file stdin_file;
};

static void shell_input_type_file_cleanup (struct shell_input_type_file *ifp)
{
    if (ifp->fds) {
        int i;
        for (i = 0; i < ifp->count; i++)
            close (ifp->fds[i]);
        free (ifp->fds);
    }
}

void shell_input_destroy (struct shell_input *in)
{
    if (in) {
        int saved_errno = errno;
        shell_input_type_file_cleanup (&(in->stdin_file));
        free (in);
        errno = saved_errno;
    }
}

static int shell_input_parse_type (struct shell_input *in)
{
    const char *typestr = NULL;
    int ret;

    if ((ret = flux_shell_getopt_unpack (in->shell, "input",
                                         "{s?:{s?:s}}",
                                         "stdin", "type", &typestr)) < 0)
        return -1;

    if (!ret || !typestr)
        return 0;

    if (!strcmp (typestr, "file")) {
        struct shell_input_type_file *ifp = &(in->stdin_file);

        in->stdin_type = FLUX_INPUT_TYPE_FILE;

        if (flux_shell_getopt_unpack (in->shell, "input",
                                      "{s:{s?:s}}",
                                      "stdin", "path", &(ifp->path)) < 0)
            return -1;

        if (ifp->path == NULL) {
            log_msg ("path for stdin file input not specified");
            return -1;
        }

        ifp->fds = malloc (sizeof (int) * in->shell->info->rankinfo.ntasks);
        if (!ifp->fds)
            return -1;
    }
    else {
        log_msg ("invalid input type specified '%s'", typestr);
        return -1;
    }

    return 0;
}

struct shell_input *shell_input_create (flux_shell_t *shell)
{
    struct shell_input *in;

    if (!(in = calloc (1, sizeof (*in))))
        return NULL;
    in->shell = shell;
    in->stdin_type = FLUX_INPUT_TYPE_NONE;

    /* Check if user specified shell input */
    if (shell_input_parse_type (in) < 0)
        goto error;

    return in;
error:
    shell_input_destroy (in);
    return NULL;
}

static int shell_input_type_file_setup (struct shell_input *in,
                                        struct shell_task *task)
{
    struct shell_input_type_file *ifp = &(in->stdin_file);
    char buf_fd[64];
    int fd = -1;
    int saved_errno;

    if ((fd = open (ifp->path, O_RDONLY)) < 0) {
        log_err ("error opening input file '%s'", ifp->path);
        goto error;
    }

    snprintf (buf_fd, sizeof (buf_fd), "%d", fd);

    if (flux_cmd_setopt (task->cmd, "stdin_INPUT_FD", buf_fd) < 0) {
        log_err ("error setting 'stdin_INPUT_FD'");
        goto error;
    }

    ifp->fds[ifp->count++] = fd;
    return 0;
error:
    saved_errno = errno;
    close (fd);
    errno = saved_errno;
    return -1;
}

static int shell_input_task_init (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_input *in = flux_plugin_aux_get (p, "builtin.input");
    flux_shell_task_t *task;

    if (!shell || !in || !(task = flux_shell_current_task (shell)))
        return -1;

    if (in->stdin_type == FLUX_INPUT_TYPE_FILE) {
        if (shell_input_type_file_setup (in, task) < 0)
            return -1;
    }

    return 0;
}

static int shell_input_init (flux_plugin_t *p,
                             const char *topic,
                             flux_plugin_arg_t *args,
                             void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_input *in = shell_input_create (shell);
    if (!in)
        return -1;
    if (flux_plugin_aux_set (p, "builtin.input", in,
                            (flux_free_f) shell_input_destroy) < 0) {
        shell_input_destroy (in);
        return -1;
    }
    return 0;
}

struct shell_builtin builtin_input = {
    .name = "input",
    .init = shell_input_init,
    .task_init = shell_input_task_init
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
