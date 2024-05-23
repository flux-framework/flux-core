/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  Start a standard input service on leader shell for shuttling input
 *  data to the KVS guest.input eventlog.
 */
#define FLUX_SHELL_PLUGIN_NAME "input.service"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "util.h"

#include "task.h"
#include "svc.h"
#include "internal.h"
#include "builtins.h"

struct input_service {
    flux_shell_t *shell;
    struct idset *open_tasks;
};

void input_service_destroy (struct input_service *in)
{
    int saved_errno = errno;
    idset_destroy (in->open_tasks);
    free (in);
    errno = saved_errno;
}

/*  Return true if idset b is a strict subset of a
 */
static bool is_subset (const struct idset *a, const struct idset *b)
{
    struct idset *isect = idset_intersect (a, b);
    if (isect) {
        bool result = idset_equal (isect, b);
        idset_destroy (isect);
        return result;
    }
    return false;
}

/*  Subtract idset 'b' from 'a', unless 'ranks' is all then clear 'a'.
 */
static int subtract_idset (struct idset *a,
                           const char *ranks,
                           struct idset *b)
{
    /*  Remove all tasks with EOF from open_tasks idset
     */
    if (streq (ranks, "all"))
        return idset_clear_all (a);
    else
        return idset_subtract (a, b);
}

/* Convert 'iodecode' object to an valid RFC 24 data event.
 * N.B. the iodecode object is a valid "context" for the event.
 */
static void input_service_stdin_cb (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct input_service *in = arg;
    bool eof = false;
    const char *ranks;
    struct idset *ids = NULL;
    json_t *o;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0)
        goto error;
    if (idset_count (in->open_tasks) == 0) {
        errno = EPIPE;
        goto error;
    }
    if (iodecode (o, NULL, &ranks, NULL, NULL, &eof) < 0)
        goto error;
    if (!streq (ranks, "all")) {
        /* Ensure that targeted tasks are still open.
         * ("all" is treated as "all open")
         */
        if (!(ids = idset_decode (ranks)))
            goto error;
        if (!is_subset (in->open_tasks, ids)) {
            errno = EPIPE;
            goto error;
        }
    }
    if (input_eventlog_put_event (in->shell, "data", o) < 0)
        goto error;
    if (eof && subtract_idset (in->open_tasks, ranks, ids) < 0)
        shell_log_errno ("failed to remove '%s' from open tasks", ranks);
    if (flux_respond (h, msg, NULL) < 0)
        shell_log_errno ("flux_respond");
    idset_destroy (ids);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        shell_log_errno ("flux_respond");
    idset_destroy (ids);
}

struct input_service *input_service_create (flux_shell_t *shell)
{
    struct input_service *in;

    if (!(in = calloc (1, sizeof (*in))))
        return NULL;
    in->shell = shell;
    if (!(in->open_tasks = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_range_set (in->open_tasks,
                            0,
                            shell->info->total_ntasks - 1))
        goto error;
    if (flux_shell_service_register (in->shell,
                                     "stdin",
                                     input_service_stdin_cb,
                                     in) < 0)
        shell_die_errno (1, "flux_shell_service_register");

    /* Do not add a completion reference for the stdin service, we
     * don't care if the user ever sends stdin */

    if (input_eventlog_init (shell) < 0)
        goto error;

    return in;
error:
    input_service_destroy (in);
    return NULL;
}

static int input_service_init (flux_plugin_t *p,
                               const char *topic,
                               flux_plugin_arg_t *args,
                               void *data)
{
    struct input_service *in;
    const char *type = "service";
    flux_shell_t *shell = flux_plugin_get_shell (p);

    /* Only active on shell rank 0
     */
    if (shell->info->shell_rank != 0)
        return 0;

    if (flux_shell_getopt_unpack (shell,
                                  "input",
                                  "{s?{s?s}}",
                                  "stdin",
                                   "type", &type) < 0)
        return -1;

    /* Check validity of input.stdin.type here. Only valid types currently
     * are "service" and "file":
     */
    if (!streq (type, "service") && !streq (type, "file"))
        return shell_log_errn (0, "input.stdin.type=%s invalid", type);

    if (!streq (type, "service"))
        return 0;

    if (!(in = input_service_create (shell)))
        return -1;
    if (flux_plugin_aux_set (p,
                             "builtin.input-service",
                             in,
                             (flux_free_f) input_service_destroy) < 0) {
        input_service_destroy (in);
        return -1;
    }
    return 0;
}

struct shell_builtin builtin_input_service = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = input_service_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
