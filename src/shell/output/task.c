/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* per-task std output handling
 *
 * This module defines a task_output abstraction for handling
 * redirection of local task output. A list of task output objects
 * is created at shell initialization. If an output file template
 * is specified, then the template is rendered for each task to
 * allow for an output-file-per task.
 *
 * Functions are provided to obtain the output "file entry" for any
 * task in the case output is to file, and also to write data
 * to the same destination as any local task rank.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>

/* Note: necessary for shell_log functions
 */
#define FLUX_SHELL_PLUGIN_NAME "output.task"

#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "internal.h"
#include "info.h"
#include "output/task.h"

struct task_output;

typedef int (*task_output_f) (struct task_output *to,
                              const char *stream,
                              const char *data,
                              int len,
                              bool eof);

struct task_output {
    struct shell_output *out;
    flux_shell_task_t *task;
    int rank;
    char rank_str[13];
    struct file_entry *stdout_fp;
    struct file_entry *stderr_fp;
    task_output_f stdout_f;
    task_output_f stderr_f;
};

struct task_output_list {
    struct shell_output *out;
    zlistx_t *task_outputs;
};

void task_output_destroy (struct task_output *to)
{
    if (to) {
        int saved_errno = errno;
        file_entry_close (to->stdout_fp);
        file_entry_close (to->stderr_fp);
        free (to);
        errno = saved_errno;
    }
}

static struct file_entry *task_open_file (struct shell_output *out,
                                          flux_shell_task_t *task,
                                          struct output_stream *stream)
{
    char *path = NULL;
    int flags = O_CREAT | O_WRONLY;
    struct file_entry *fp = NULL;
    flux_error_t error;

    if (streq (stream->mode, "append"))
        flags |= O_APPEND;
    else if (streq (stream->mode, "truncate"))
        flags |= O_TRUNC;
    else
        shell_warn ("ignoring invalid output.mode=%s", stream->mode);

    if (stream->template == NULL) {
        shell_log_error ("path for file output not specified");
        return NULL;
    }

    if (!(path = flux_shell_task_mustache_render (out->shell,
                                                  task,
                                                  stream->template)))
        return NULL;

    if (!(fp = filehash_open (out->files, &error, path, flags, stream->label)))
        shell_log_error ("%s", error.text);
    free (path);
    return fp;
}

static json_t *task_output_ioencode (struct task_output *to,
                                     const char *stream,
                                     const char *data,
                                     int len,
                                     bool eof)
{
    json_t *o;
    if (!(o = ioencode (stream, to->rank_str, data, len, eof)))
        shell_log_errno ("ioencode");
    return o;
}

static int task_output_write_client (struct task_output *to,
                                     const char *stream,
                                     const char *data,
                                     int len,
                                     bool eof)
{
    int rc;
    json_t *o;

    if (!(o = task_output_ioencode (to, stream, data, len, eof)))
        return -1;
    rc = output_client_send (to->out->client, "data", o);
    json_decref (o);
    return rc;
}

static int task_output_write_kvs (struct task_output *to,
                                  const char *stream,
                                  const char *data,
                                  int len,
                                  bool eof)
{
    int rc;
    json_t *o;

    if (!(o = task_output_ioencode (to, stream, data, len, eof)))
        return -1;
    rc = kvs_output_write_entry (to->out->kvs, "data", o);
    json_decref (o);
    return rc;
}

static int task_output_write_file (struct task_output *to,
                                   const char *stream,
                                   const char *data,
                                   int len,
                                   bool eof)
{
    struct file_entry *fp = to->stderr_fp;
    if (streq (stream, "stdout"))
        fp = to->stdout_fp;
    if (file_entry_write (fp, to->rank_str, data, len) < 0)
        return -1;
    return 0;
}

static struct task_output *task_output_create (struct shell_output *out,
                                               flux_shell_task_t *task)
{
    struct task_output *to;
    int rank;

    if (flux_shell_task_info_unpack (task,
                                     "{s:i}",
                                     "rank", &rank) < 0)
        return NULL;

    if (!(to = calloc (1, sizeof (*to))))
        return NULL;
    to->out = out;
    to->task = task;
    to->rank = rank;

    /* Note: %d guaranteed to fit in 13 bytes:
     */
    (void) snprintf (to->rank_str, sizeof (to->rank_str), "%d", rank);

    if (out->shell->info->shell_rank == 0) {
        /* rank 0: if stdout/err are files then task writes to file,
         * otherwise KVS.
         */
        if (out->conf->out.type == FLUX_OUTPUT_TYPE_FILE) {
            if (!(to->stdout_fp = task_open_file (out, task, &out->conf->out)))
                goto error;
            to->stdout_f = task_output_write_file;
        }
        else
            to->stdout_f = task_output_write_kvs;

        if (out->conf->err.type == FLUX_OUTPUT_TYPE_FILE) {
            if (!(to->stderr_fp = task_open_file (out, task, &out->conf->err)))
                goto error;
            to->stderr_f = task_output_write_file;
        }
        else
            to->stderr_f = task_output_write_kvs;
    }
    else {
        /* Other shell ranks: client writer unless per-shell output
         */
        if (out->conf->out.per_shell) {
            if (!(to->stdout_fp = task_open_file (out, task, &out->conf->out)))
                goto error;
            to->stdout_f = task_output_write_file;
        }
        else
            to->stdout_f = task_output_write_client;

        if (out->conf->err.per_shell) {
            if (!(to->stderr_fp = task_open_file (out, task, &out->conf->err)))
                goto error;
            to->stderr_f = task_output_write_file;
        }
        else
            to->stderr_f = task_output_write_client;
    }
    return to;
error:
    task_output_destroy (to);
    return NULL;
}

static task_output_f task_write_fn (struct task_output *to,
                                    const char *stream)
{
    if (streq (stream, "stdout"))
        return to->stdout_f;
    return to->stderr_f;
}

static void task_write (struct task_output *to,
                        const char *stream,
                        const char *data,
                        int len)
{
    task_output_f fn = task_write_fn (to, stream);
    flux_subprocess_t *proc = flux_shell_task_subprocess (to->task);

    if (len > 0) {
        if ((*fn) (to, stream, data, len, false) < 0)
            shell_log_errno ("write %s task %d", stream, to->rank);
    }
    else if (flux_subprocess_read_stream_closed (proc, stream)) {
        if ((*fn) (to, stream, NULL, 0, true) < 0)
            shell_log_errno ("write eof %s task %d", stream, to->rank);
    }
}

static void task_none_output_cb (struct shell_task *task,
                                 const char *stream,
                                 void *arg)
{
    struct task_output *to = arg;
    flux_subprocess_t *proc = flux_shell_task_subprocess (to->task);
    const char *data;
    int len;

    /* Attempt to read a line. If this fails, get whatever data is
     * available since this function handles unbuffered output.
     */
    len = flux_subprocess_read_line (proc, stream, &data);
    if (len < 0) {
        shell_log_errno ("read line %s task %d", stream, to->rank);
    }
    else if (!len) {
        if ((len = flux_subprocess_read (proc, stream, &data)) < 0) {
            shell_log_errno ("read %s task %d", stream, to->rank);
            return;
        }
    }
    task_write (to, stream, data, len);
}

static void task_line_output_cb (struct shell_task *task,
                                 const char *stream,
                                 void *arg)
{
    struct task_output *to = arg;
    flux_subprocess_t *proc = flux_shell_task_subprocess (to->task);
    const char *data;
    int len;

    len = flux_subprocess_getline (proc, stream, &data);
    if (len < 0) {
        shell_log_errno ("read %s task %d", stream, to->rank);
        return;
    }
    task_write (to, stream, data, len);
}

static int task_output_setup_stream (struct task_output *to,
                                     const char *name,
                                     struct output_stream *stream)
{
    flux_shell_task_t *task = to->task;
    flux_cmd_t *cmd = flux_shell_task_cmd (task);
    void (*output_cb) (struct shell_task *, const char *, void *);

    /* libsubprocess default is to buffer output by line, so only
     * check for buffer_type of "none" and handle alternate case here:
     */
    output_cb = task_line_output_cb;
    if (!strcasecmp (stream->buffer_type, "none")) {
        char buf[64];
        snprintf (buf, sizeof (buf), "%s_LINE_BUFFER", name);
        if (flux_cmd_setopt (cmd, buf, "false") < 0) {
            shell_log_errno ("flux_cmd_setopt");
            return -1;
        }
        output_cb = task_none_output_cb;
    }

    /* Subscribe to this task channel with appropriate buffering:
     */
    if (flux_shell_task_channel_subscribe (task, name, output_cb, to) < 0)
        return -1;
    return 0;
}

static void task_output_destructor (void **item)
{
    if (item) {
        struct task_output *to = *item;
        task_output_destroy (to);
        *item = NULL;
    }
}

void task_output_list_destroy (struct task_output_list *l)
{
    if (l) {
        int saved_errno = errno;
        zlistx_destroy (&l->task_outputs);
        free (l);
        errno = saved_errno;
    }
}

struct task_output_list *task_output_list_create (struct shell_output *out)
{
    struct task_output_list *l;
    struct task_output *to;
    flux_shell_task_t *task;

    if (!(l = calloc (1, sizeof (*l)))
        || !(l->task_outputs = zlistx_new ()))
        goto error;
    zlistx_set_destructor (l->task_outputs, task_output_destructor);
    l->out = out;

    /* Create all task output objects so that any per-task files are opened
     */
    task = flux_shell_task_first (out->shell);
    while (task) {
        /*  Create task output object, subscribe to stdout/stderr from task
         *  and add task output object to task outputs list
         */
        if (!(to = task_output_create (out, task))
            || task_output_setup_stream (to, "stdout", &out->conf->out) < 0
            || task_output_setup_stream (to, "stderr", &out->conf->err) < 0
            || !zlistx_add_end (l->task_outputs, to))
            goto error;
        task = flux_shell_task_next (out->shell);
    }
    return l;
error:
    task_output_list_destroy (l);
    return NULL;
}

struct file_entry *task_output_file_entry (struct task_output_list *l,
                                           char *stream,
                                           int index)
{
    int n = 0;
    struct task_output *to;

    to = zlistx_first (l->task_outputs);
    while (to) {
        if (n == index) {
            struct file_entry *fp = to->stderr_fp;
            if (streq (stream, "stdout"))
                fp = to->stdout_fp;
            return filehash_entry_incref (fp);
        }
        to = zlistx_next (l->task_outputs);
        n++;
    }
    errno = ENOENT;
    return NULL;
}

static int str2rank (const char *s)
{
    long l;
    char *p;

    errno = 0;
    l = strtol (s, &p, 10);
    if (errno != 0 || *p != '\0' || l < 0) {
        shell_log_error ("error converting '%s' to rank", s);
        return -1;
    }
    return (int) l;
}

int task_output_list_write (struct task_output_list *l,
                            json_t *context)
{
    const char *rankstr;
    const char *stream;
    const void *data;
    size_t len;
    int rank;
    struct task_output *to;

    if (json_unpack (context,
                     "{s:s s:s s:s%}",
                     "stream", &stream,
                     "rank", &rankstr,
                     "data", &data, &len) < 0)
        return -1;

    if ((rank = str2rank (rankstr)) < 0)
        return -1;

    to = zlistx_first (l->task_outputs);
    while (to) {
        if (to->rank == rank) {
            task_write (to, stream, data, len);
            return 0;
        }
        to = zlistx_next (l->task_outputs);
    }
    errno = ENOENT;
    return -1;
}

/* vi: ts=4 sw=4 expandtab
 */
