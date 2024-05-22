/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  If stdin type is "kvs", watch guest.input eventlog and send
 *  input data to all local tasks.
 */
#define FLUX_SHELL_PLUGIN_NAME "input.kvs"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "task.h"
#include "svc.h"
#include "internal.h"
#include "builtins.h"

struct task_input_kvs {
    flux_shell_t *shell;
    bool header_parsed;
    flux_future_t *input_f;
};

static void task_input_kvs_destroy (struct task_input_kvs *kp)
{
    if (kp) {
        int saved_errno = errno;
        flux_future_destroy (kp->input_f);
        free (kp);
        errno = saved_errno;
    }
}

static struct task_input_kvs *task_input_kvs_create (flux_shell_t *shell)
{
    struct task_input_kvs *kp;
    if (!(kp = calloc (1, sizeof (*kp))))
        return NULL;
    kp->shell = shell;
    return kp;
}

/*  Return 1 if idset string 'set' contains the integer id.
 *  O/w, return 0, or -1 on failure to decode 'set'.
 */
static int idset_string_contains (const char *set, uint32_t id)
{
    int rc;
    struct idset *idset;
    if (streq (set, "all"))
        return 1;
    if (!(idset = idset_decode (set)))
        return shell_log_errno ("idset_decode (%s)", set);
    rc = idset_test (idset, id);
    idset_destroy (idset);
    return rc;
}

static void input_eventlog_cb (flux_future_t *f, void *arg)
{
    struct task_input_kvs *kp = arg;
    const char *entry;
    json_t *o;
    const char *name;
    json_t *context;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            return;
        shell_die (1,
                   "flux_job_event_watch_get: %s",
                   future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        shell_die_errno (1, "eventlog_entry_decode");
    if (eventlog_entry_parse (o, NULL, &name, &context) < 0)
        shell_die_errno (1, "eventlog_entry_parse");

    if (streq (name, "header")) {
        /* Future: per-stream encoding */
        kp->header_parsed = true;
    }
    else if (streq (name, "data")) {
        flux_shell_task_t *task;
        char *data = NULL;
        const char *rank = NULL;
        const char *stream = NULL;
        int len;
        bool eof;

        if (!kp->header_parsed)
            shell_die (1, "stream data read before header");

        if (iodecode (context, &stream, &rank, &data, &len, &eof) < 0)
            shell_die (1, "malformed input event context");

        /*  broadcast input to all matching tasks
         */
        task = flux_shell_task_first (kp->shell);
        while (task != NULL) {
            if (idset_string_contains (rank, task->rank) == 1) {
                if (len > 0) {
                    if (flux_subprocess_write (task->proc,
                                               stream,
                                               data,
                                               len) < 0) {
                        if (errno != EPIPE)
                            shell_die_errno (1, "flux_subprocess_write");
                        else
                            eof = true; /* Pretend that we got eof */
                    }
                }
                if (eof) {
                    if (flux_subprocess_close (task->proc, stream) < 0)
                        shell_die_errno (1, "flux_subprocess_close");
                }
            }
            task = flux_shell_task_next (kp->shell);
        }
        free (data);
    }
    json_decref (o);
    flux_future_reset (f);
    return;
}

static int task_input_kvs_start (struct task_input_kvs *kp)
{
    flux_future_t *f = NULL;

    /*  Start watching kvs guest.input eventlog.
     *  Since this function is called after shell initialization
     *   barrier, we are guaranteed that input eventlog exists.
     */
    if (!(f = flux_job_event_watch (kp->shell->h,
                                    kp->shell->info->jobid,
                                    "guest.input",
                                    0)))
            shell_die_errno (1, "flux_job_event_watch");

    if (flux_future_then (f, -1., input_eventlog_cb, kp) < 0) {
        flux_future_destroy (f);
        shell_die_errno (1, "flux_future_then");
    }
    kp->input_f = f;
    return 0;
}


static int input_kvs_start (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct task_input_kvs *kp;
    const char *type = "service";

    /*  No need to watch kvs input eventlog if input mode is not "kvs"
     *  or unset.
     */
    if (flux_shell_getopt_unpack (shell,
                                  "input",
                                  "{s?{s?s}}",
                                  "stdin",
                                   "type", &type) < 0)
        return -1;

    if (!streq (type, "service"))
        return 0;


    if (!(kp = task_input_kvs_create (shell))
        || flux_plugin_aux_set (p,
                                NULL,
                                kp,
                                (flux_free_f) task_input_kvs_destroy) < 0) {
        task_input_kvs_destroy (kp);
        return -1;
    }
    if (task_input_kvs_start (kp) < 0)
        return -1;
    return 0;
}

struct shell_builtin builtin_kvs_input = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .start = input_kvs_start,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
