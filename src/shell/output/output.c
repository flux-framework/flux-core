/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* std output handling
 *
 * Intercept task stdout, stderr and dispose of it according to
 * selected I/O mode.
 *
 * See component specific source files for details of operation.
 * These include:
 *
 * output/conf.[ch]:     jobspec output file options reader
 * output/service.[ch]:  std output write service implementation
 * output/client.[ch]:   std output leader service client
 * output/kvs.[ch]:      std output kvs writer
 * output/filehash.[ch]: std output file hash and writer
 * output/log.[ch]:      shell.log output handling
 * output/task.[ch]:     per-task std output handling
 *
 */
#define FLUX_SHELL_PLUGIN_NAME "output"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <unistd.h>
#include <fcntl.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "task.h"
#include "svc.h"
#include "internal.h"
#include "builtins.h"
#include "log.h"
#include "output/filehash.h"
#include "output/client.h"
#include "output/kvs.h"
#include "output/conf.h"
#include "output/output.h"
#include "output/task.h"
#include "output/service.h"
#include "output/log.h"


static int shell_output_file_data (struct shell_output *out, json_t *context)
{
    struct file_entry *fp;
    const char *stream = NULL;
    const char *rank = NULL;
    char *data = NULL;
    int len = 0;
    int rc = -1;

    if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0) {
        shell_log_errno ("iodecode");
        return -1;
    }
    if (streq (stream, "stdout"))
        fp = out->stdout_fp;
    else
        fp = out->stderr_fp;

    if (file_entry_write (fp, rank, data, len) < 0)
        goto out;
    rc = 0;
out:
    free (data);
    return rc;
}

static int shell_output_file (struct shell_output *out,
                              const char *name,
                              json_t *context)
{
    if (streq (name, "data")) {
        if (shell_output_file_data (out, context) < 0) {
            shell_log_errno ("shell_output_file_data");
            return -1;
        }
    }
    else if (streq (name, "log")) {
        shell_output_log_file (out, context);
    }
    return 0;
}

int shell_output_write_entry (struct shell_output *out,
                              const char *type,
                              json_t *o)
{
    struct file_entry *fp = out->stderr_fp;

    if (streq (type, "data")) {
        const char *stream = "stderr"; // default to stderr
        (void) iodecode (o, &stream, NULL, NULL, NULL, NULL);
        if (streq (stream, "stdout"))
            fp = out->stdout_fp;
    }
    /* If there's an output file for this stream, write entry there:
     */
    if (fp)
        return shell_output_file (out, type, o);

    /* O/w, if this is not rank 0, then send RPC to leader shell:
     */
    if (out->shell->info->shell_rank != 0)
        return output_client_send (out->client, type, o);

    /* O/w, this is the leader shell and destination is KVS:
     */
    return kvs_output_write_entry (out->kvs, type, o);
}

static void shell_output_close (struct shell_output *out)
{
    file_entry_close (out->stdout_fp);
    file_entry_close (out->stderr_fp);
    kvs_output_close (out->kvs);
}

void shell_output_incref (struct shell_output *out)
{
    if (out)
        out->refcount++;
}

void shell_output_decref (struct shell_output *out)
{
    if (out && --out->refcount == 0)
        shell_output_close (out);
}

static int shell_output_handler (flux_plugin_t *p,
                                 const char *topic,
                                 flux_plugin_arg_t *args,
                                 void *arg)
{
    struct shell_output *out = arg;
    json_t *context;

    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN, "o", &context) < 0) {
        shell_log_errno ("shell.output: flux_plugin_arg_unpack");
        return -1;
    }
    return task_output_list_write (out->task_outputs, context);
}

static void shell_output_destroy (struct shell_output *out)
{
    if (out) {
        int saved_errno = errno;
        output_config_destroy (out->conf);
        output_client_destroy (out->client);
        task_output_list_destroy (out->task_outputs);
        output_service_destroy (out->service);
        filehash_destroy (out->files);
        kvs_output_destroy (out->kvs);
        free (out);
        errno = saved_errno;
    }
}

static int output_redirect_stream (struct shell_output *out,
                                   const char *name,
                                   struct output_stream *stream)
{
    if (stream->type == FLUX_OUTPUT_TYPE_FILE) {
        /*  Note: per-rank or per-task redirect events are not generated
         *  at this time. flux_shell_rank_mustache_render() with invalid
         *  rank will leave any task/node specific tags unexpanded in the
         *  posted path, e.g. flux-{{node.id}}-{{task.id}.out:
         */
        char *path;
        int shell_size = out->shell->info->shell_size;
        if (!(path = flux_shell_rank_mustache_render (out->shell,
                                                      shell_size,
                                                      stream->template))
            || kvs_output_redirect (out->kvs, name, path) < 0)
            shell_log_errno ("failed to post %s redirect event", name);
        free (path);
    }
    return 0;
}

static int shell_output_redirect (struct shell_output *out)
{
    if (output_redirect_stream (out, "stdout", &out->conf->out) < 0
        || output_redirect_stream (out, "stderr", &out->conf->err) < 0)
        return -1;
    return 0;
}

struct shell_output *shell_output_create (flux_plugin_t *p,
                                          flux_shell_t *shell)
{
    struct shell_output *out;

    if (!(out = calloc (1, sizeof (*out))))
        return NULL;
    out->shell = shell;

    if (!(out->conf = output_config_create (shell)))
        goto error;

    if (!(out->files = filehash_create ()))
        goto error;

    if (!(out->task_outputs = task_output_list_create (out)))
        goto error;

    if (shell->info->shell_rank == 0) {
        int size = shell->info->shell_size;

        /* Create 'shell.write' service
         */
        if (!(out->service = output_service_create (out, p, size)))
            goto error;

        /* Create kvs output eventlog + header
         */
        if (!(out->kvs = kvs_output_create (shell)))
            goto error;

        /* If output is redirected to a file, post redirect event(s) to KVS
         */
        if (shell_output_redirect (out) < 0)
            goto error;

        /* Flush kvs output so eventlog is created
         */
        kvs_output_flush (out->kvs);
    }
    else if (!(out->client = output_client_create (shell))) {
        shell_log_errno ("failed to create output service client");
        goto error;
    }
    return out;
error:
    shell_output_destroy (out);
    return NULL;
}

static int shell_output_task_init (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    struct shell_output *out = flux_plugin_aux_get (p, "builtin.output");
    /* Add output reference for this task
     */
    shell_output_incref (out);
    return 0;
}


static int shell_output_task_exit (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    struct shell_output *out = flux_plugin_aux_get (p, "builtin.output");
    /* Remove output reference for this task
     */
    shell_output_decref (out);
    return 0;
}

static void shell_output_setup_file_entries (flux_plugin_t *p,
                                             struct shell_output *out)
{
    /* Set shell-wide stdout/stderr to go to the same place as the first
     * task. This is used for log information, and on rank 0 if there is
     * is only one output file for stdout and/or stderr.
     *
     * Note: these members are expected to be NULL if output is being
     * sent to the KVS for one or both streams.
     */
    out->stdout_fp = task_output_file_entry (out->task_outputs, "stdout", 0);
    out->stderr_fp = task_output_file_entry (out->task_outputs, "stderr", 0);
    shell_output_log_init (p, out);
}

static int shell_output_init (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_output *out = shell_output_create (p, shell);
    if (!out)
        return -1;

    shell_output_setup_file_entries (p, out);

    if (flux_plugin_aux_set (p,
                             "builtin.output",
                             out,
                             (flux_free_f) shell_output_destroy) < 0) {
        shell_output_destroy (out);
        return -1;
    }
    if (flux_plugin_add_handler (p,
                                 "shell.output",
                                 shell_output_handler,
                                 out) < 0) {
        shell_output_destroy (out);
        return -1;
    }
    return 0;
}

static int shell_output_reconnect (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    struct shell_output *out = data;
    kvs_output_reconnect (out->kvs);
    return 0;
}

struct shell_builtin builtin_output = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .reconnect = shell_output_reconnect,
    .init = shell_output_init,
    .task_init = shell_output_task_init,
    .task_exit = shell_output_task_exit,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
