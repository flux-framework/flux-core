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
 * Depending on inputs from user, a service is started to receive
 * stdin from front-end command or file is read for redirected
 * standard input.
 */
#define FLUX_SHELL_PLUGIN_NAME "input"

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

#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "task.h"
#include "svc.h"
#include "internal.h"
#include "builtins.h"

struct shell_input;

/* input type configured by user for input to the shell */
enum {
    FLUX_INPUT_TYPE_SERVICE = 1, /* default */
    FLUX_INPUT_TYPE_FILE = 2,
};

/* how input will reach each task */
enum {
    FLUX_TASK_INPUT_KVS = 1,
};

struct shell_task_input_kvs {
    flux_future_t *input_f;
    bool input_header_parsed;
};

struct shell_task_input {
    struct shell_input *in;
    struct shell_task *task;
    int type;
    struct shell_task_input_kvs input_kvs;
};

struct shell_input_type_file {
    const char *path;
    int fd;
    flux_watcher_t *w;
    char *rankstr;
};

struct shell_input {
    flux_shell_t *shell;
    int stdin_type;
    struct shell_task_input *task_inputs;
    int ntasks;
    struct shell_input_type_file stdin_file;
};

static void shell_task_input_kvs_cleanup (struct shell_task_input_kvs *kp)
{
    flux_future_destroy (kp->input_f);
    kp->input_f = NULL;
}

static void shell_task_input_cleanup (struct shell_task_input *tp)
{
    shell_task_input_kvs_cleanup (&(tp->input_kvs));
}

static void shell_input_type_file_cleanup (struct shell_input_type_file *fp)
{
    close (fp->fd);
    flux_watcher_destroy (fp->w);
    free (fp->rankstr);
}

void shell_input_destroy (struct shell_input *in)
{
    if (in) {
        int saved_errno = errno;
        int i;
        shell_input_type_file_cleanup (&(in->stdin_file));
        for (i = 0; i < in->ntasks; i++)
            shell_task_input_cleanup (&(in->task_inputs[i]));
        free (in->task_inputs);
        free (in);
        errno = saved_errno;
    }
}

static void shell_input_put_kvs_completion (flux_future_t *f, void *arg)
{
    struct shell_input *in = arg;

    if (flux_future_get (f, NULL) < 0)
        /* failng to write stdin to input is a fatal error */
        shell_die (1, "shell_input_put_kvs: %s", strerror (errno));
    flux_future_destroy (f);

    if (flux_shell_remove_completion_ref (in->shell, "input.kvs") < 0)
        shell_log_errno ("flux_shell_remove_completion_ref");
}

static int shell_input_put_kvs (struct shell_input *in, json_t *context)
{
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    json_t *entry = NULL;
    char *entrystr = NULL;
    int saved_errno;
    int rc = -1;

    if (!(entry = eventlog_entry_pack (0.0, "data", "O", context)))
        goto error;
    if (!(entrystr = eventlog_entry_encode (entry)))
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "input", entrystr) < 0)
        goto error;
    if (!(f = flux_kvs_commit (in->shell->h, NULL, 0, txn)))
        goto error;
    if (flux_future_then (f, -1, shell_input_put_kvs_completion, in) < 0)
        goto error;
    if (flux_shell_add_completion_ref (in->shell, "input.kvs") < 0) {
        shell_log_errno ("flux_shell_remove_completion_ref");
        goto error;
    }
    /* f memory responsibility of shell_input_put_kvs_completion()
     * callback */
    f = NULL;
    rc = 0;
 error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    free (entrystr);
    json_decref (entry);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

/* Convert 'iodecode' object to an valid RFC 24 data event.
 * N.B. the iodecode object is a valid "context" for the event.
 */
static void shell_input_stdin_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct shell_input *in = arg;
    bool eof = false;
    json_t *o;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0)
        goto error;
    if (iodecode (o, NULL, NULL, NULL, NULL, &eof) < 0)
        goto error;
    if (shell_input_put_kvs (in, o) < 0)
        goto error;
    if (flux_respond (in->shell->h, msg, NULL) < 0)
        shell_log_errno ("flux_respond");
    return;
error:
    if (flux_respond_error (in->shell->h, msg, errno, NULL) < 0)
        shell_log_errno ("flux_respond");
}

static void shell_input_type_file_init (struct shell_input *in)
{
    struct shell_input_type_file *fp = &(in->stdin_file);
    fp->fd = -1;
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

    if (streq (typestr, "service"))
        in->stdin_type = FLUX_INPUT_TYPE_SERVICE;
    else if (streq (typestr, "file")) {
        struct shell_input_type_file *fp = &(in->stdin_file);

        in->stdin_type = FLUX_INPUT_TYPE_FILE;

        if (flux_shell_getopt_unpack (in->shell, "input",
                                      "{s:{s?:s}}",
                                      "stdin", "path", &(fp->path)) < 0)
            return -1;

        if (fp->path == NULL)
            return shell_log_errn (0,
                                   "path for stdin file input not specified");
    }
    else
        return shell_log_errn (0, "invalid input type specified '%s'", typestr);

    return 0;
}

static int shell_input_kvs_init (struct shell_input *in, json_t *header)
{
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    char *headerstr = NULL;
    int saved_errno;
    int rc = -1;

    if (!(headerstr = eventlog_entry_encode (header)))
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "input", headerstr) < 0)
        goto error;
    if (!(f = flux_kvs_commit (in->shell->h, NULL, 0, txn)))
        goto error;
    /* Synchronously wait for kvs commit to complete to ensure
     * guest.input exists before passing shell initialization barrier.
     * This is required because tasks will immediately try to watch
     * input eventlog on starting.
     */
    if (flux_future_get (f, NULL) < 0)
        shell_die_errno (1, "failed to create input eventlog");
    rc = 0;
 error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    free (headerstr);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

static int shell_input_header (struct shell_input *in)
{
    json_t *o = NULL;
    int rc = -1;

    o = eventlog_entry_pack (0, "header",
                             "{s:i s:{s:s} s:{s:i} s:{}}",
                             "version", 1,
                             "encoding",
                             "stdin", "UTF-8",
                             "count",
                             "stdin", 1,
                             "options");
    if (!o) {
        errno = ENOMEM;
        goto error;
    }
    if (shell_input_kvs_init (in, o) < 0) {
        shell_log_errno ("shell_input_kvs_init");
        goto error;
    }
    rc = 0;
 error:
    json_decref (o);
    return rc;
}

static int shell_input_put_kvs_raw (struct shell_input *in,
                                    void *buf,
                                    int len,
                                    bool eof)
{
    json_t *context = NULL;
    int saved_errno;
    int rc = -1;

    if (!(context = ioencode ("stdin", in->stdin_file.rankstr, buf, len, eof)))
        goto error;
    if (shell_input_put_kvs (in, context) < 0)
        goto error;
    rc = 0;
 error:
    saved_errno = errno;
    json_decref (context);
    errno = saved_errno;
    return rc;
}

static void shell_input_type_file_cb (flux_reactor_t *r, flux_watcher_t *w,
                                      int revents, void *arg)
{
    struct shell_input *in = arg;
    struct shell_input_type_file *fp = &(in->stdin_file);
    long ps = sysconf (_SC_PAGESIZE);
    char buf[ps];
    ssize_t n;

    assert (ps > 0);

    /* Failure to read stdin in a fatal error.  Should be cleaner in
     * future.  Issue #2378 */

    while ((n = read (fp->fd, buf, ps)) > 0) {
        if (shell_input_put_kvs_raw (in, buf, n, false) < 0)
            shell_die_errno (1, "shell_input_put_kvs_raw");
    }

    if (n < 0)
        shell_die_errno (1, "shell_input_put_kvs_raw");

    if (shell_input_put_kvs_raw (in, NULL, 0, true) < 0)
        shell_die_errno (1, "shell_input_put_kvs_raw");

    flux_watcher_stop (w);
}

static int shell_input_type_file_setup (struct shell_input *in)
{
    struct shell_input_type_file *fp = &(in->stdin_file);

    if ((fp->fd = open (fp->path, O_RDONLY)) < 0)
        return shell_log_errno ("error opening input file '%s'", fp->path);

    if (!(fp->w = flux_fd_watcher_create (in->shell->r, fp->fd,
                                          FLUX_POLLIN,
                                          shell_input_type_file_cb,
                                          in)))
        return shell_log_errno ("flux_fd_watcher_create");

    if (in->shell->info->total_ntasks > 1) {
        if (asprintf (&fp->rankstr, "[0-%d]",
                      in->shell->info->total_ntasks) < 0)
            return shell_log_errno ("asprintf");
    }
    else {
        if (!(fp->rankstr = strdup ("0")))
            return shell_log_errno ("asprintf");
    }

    return 0;
}

struct shell_input *shell_input_create (flux_shell_t *shell)
{
    struct shell_input *in;
    size_t task_inputs_size;
    int i;

    if (!(in = calloc (1, sizeof (*in))))
        return NULL;
    in->shell = shell;
    in->stdin_type = FLUX_INPUT_TYPE_SERVICE;
    in->ntasks = shell->info->rankinfo.ntasks;

    task_inputs_size = sizeof (struct shell_task_input) * in->ntasks;
    if (!(in->task_inputs = calloc (1, task_inputs_size)))
        goto error;

    for (i = 0; i < in->ntasks; i++)
        in->task_inputs[i].type = FLUX_TASK_INPUT_KVS;

    shell_input_type_file_init (in);

    /* Check if user specified shell input */
    if (shell_input_parse_type (in) < 0)
        goto error;

    if (shell->info->shell_rank == 0) {
        /* can't use stdin in standalone, no kvs to write to */
        if (!in->shell->standalone) {
            if (in->stdin_type == FLUX_INPUT_TYPE_SERVICE) {
                if (flux_shell_service_register (in->shell,
                                                 "stdin",
                                                 shell_input_stdin_cb,
                                                 in) < 0)
                    shell_die_errno (1, "flux_shell_service_register");

                /* Do not add a completion reference for the stdin service, we
                 * don't care if the user ever sends stdin */
            }

            if (shell_input_header (in) < 0)
                goto error;

            if (in->stdin_type == FLUX_INPUT_TYPE_FILE) {
                if (shell_input_type_file_setup (in) < 0)
                    goto error;
                /* Ok to start fd watcher now since shell_input_header()
                 *  synchronously write guest.input header.
                 */
                flux_watcher_start (in->stdin_file.w);
            }
        }
    }

    return in;
error:
    shell_input_destroy (in);
    return NULL;
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

static void shell_task_input_kvs_input_cb (flux_future_t *f, void *arg)
{
    struct shell_task_input *task_input = arg;
    struct shell_task_input_kvs *kp = &(task_input->input_kvs);
    const char *entry;
    json_t *o;
    const char *name;
    json_t *context;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        shell_die (1, "flux_job_event_watch_get: %s",
                   future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        shell_die_errno (1, "eventlog_entry_decode");
    if (eventlog_entry_parse (o, NULL, &name, &context) < 0)
        shell_die_errno (1, "eventlog_entry_parse");

    if (streq (name, "header")) {
        /* Future: per-stream encoding */
        kp->input_header_parsed = true;
    }
    else if (streq (name, "data")) {
        flux_shell_task_t *task = task_input->task;
        const char *rank = NULL;
        if (!kp->input_header_parsed)
            shell_die (1, "stream data read before header");
        if (iodecode (context, NULL, &rank, NULL, NULL, NULL) < 0)
            shell_die (1, "malformed event context");
        if (idset_string_contains (rank, task->rank) == 1) {
            const char *stream;
            char *data = NULL;
            int len;
            bool eof;
            if (iodecode (context, &stream, NULL, &data, &len, &eof) < 0)
                shell_die (1, "malformed event context");
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
                if (flux_job_event_watch_cancel (f) < 0)
                    shell_die_errno (1, "flux_job_event_watch_cancel");
            }
            free (data);
        }
    }
    json_decref (o);
    flux_future_reset (f);
    return;
done:
    shell_task_input_kvs_cleanup (kp);
}

static int shell_task_input_kvs_start (struct shell_task_input *ti)
{
    struct shell_task_input_kvs *kp = &(ti->input_kvs);
    flux_future_t *f = NULL;
    /*  Start watching kvs guest.input eventlog.
     *  Since this function is called after shell initialization
     *   barrier, we are guaranteed that input eventlog exists.
     */
    if (!(f = flux_job_event_watch (ti->in->shell->h,
                                    ti->in->shell->info->jobid,
                                    "guest.input",
                                    0)))
            shell_die_errno (1, "flux_job_event_watch");

    if (flux_future_then (f, -1., shell_task_input_kvs_input_cb, ti) < 0) {
        flux_future_destroy (f);
        shell_die_errno (1, "flux_future_then");
    }
    kp->input_f = f;
    return 0;
}

static struct shell_task_input *get_task_input (struct shell_input *in,
                                                flux_shell_task_t *task)
{
    return &in->task_inputs[task->index];
}

static int shell_input_task_init (flux_plugin_t *p,
                                  const char *topic,
                                  flux_plugin_arg_t *args,
                                  void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_input *in = flux_plugin_aux_get (p, "builtin.input");
    struct shell_task_input *task_input;
    flux_shell_task_t *task;

    if (!shell || !in || !(task = flux_shell_current_task (shell)))
        return -1;

    task_input = get_task_input (in, task);
    task_input->in = in;
    task_input->task = task;

    if (task_input->type == FLUX_TASK_INPUT_KVS) {
        /* can't read stdin in standalone mode, no KVS to read from */
        if (!task_input->in->shell->standalone
            && shell_task_input_kvs_start (task_input) < 0)
            shell_die_errno (1, "shell_input_start_task_watch");
    }
    return 0;
}

static int shell_input_task_exit (flux_plugin_t *p,
                                  const char *topic,
                                  flux_plugin_arg_t *args,
                                  void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task = flux_shell_current_task (shell);
    struct shell_input *in = flux_plugin_aux_get (p, "builtin.input");
    struct shell_task_input *task_input;

    if (!shell || !in || !task)
        return -1;

    task_input = get_task_input (in, task);
    if (task_input->type == FLUX_TASK_INPUT_KVS
        && task_input->input_kvs.input_f) {
        if (flux_job_event_watch_cancel (task_input->input_kvs.input_f) < 0)
            shell_log_errno ("flux_job_event_watch_cancel");
    }
    return 0;
}

struct shell_builtin builtin_input = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = shell_input_init,
    .task_init = shell_input_task_init,
    .task_exit = shell_input_task_exit
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
