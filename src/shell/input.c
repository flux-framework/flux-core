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
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libioencode/ioencode.h"

#include "task.h"
#include "internal.h"
#include "builtins.h"

struct shell_input;

/* input type configured by user for input to the shell */
enum {
    FLUX_INPUT_TYPE_NONE = 1,
    FLUX_INPUT_TYPE_FILE = 2,
};

/* how input will reach each task */
enum {
    FLUX_TASK_INPUT_KVS = 1,
};

struct shell_task_input_kvs {
    flux_future_t *exec_f;
    flux_future_t *input_f;
    bool input_header_parsed;
    bool eof_reached;
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
    int task_inputs_count;
    int ntasks;
    struct shell_input_type_file stdin_file;
};

static void shell_task_input_kvs_cleanup (struct shell_task_input_kvs *kp)
{
    flux_future_destroy (kp->exec_f);
    flux_future_destroy (kp->input_f);
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

    if (!strcmp (typestr, "file")) {
        struct shell_input_type_file *fp = &(in->stdin_file);

        in->stdin_type = FLUX_INPUT_TYPE_FILE;

        if (flux_shell_getopt_unpack (in->shell, "input",
                                      "{s:{s?:s}}",
                                      "stdin", "path", &(fp->path)) < 0)
            return -1;

        if (fp->path == NULL) {
            log_msg ("path for stdin file input not specified");
            return -1;
        }
    }
    else {
        log_msg ("invalid input type specified '%s'", typestr);
        return -1;
    }

    return 0;
}

/* log entry to exec.eventlog that we've created the input eventlog */
static int shell_input_ready (struct shell_input *in, flux_kvs_txn_t *txn)
{
    json_t *entry = NULL;
    char *entrystr = NULL;
    const char *key = "exec.eventlog";
    int saved_errno, rc = -1;

    if (!(entry = eventlog_entry_create (0., "input-ready", NULL))) {
        log_err ("eventlog_entry_create");
        goto error;
    }
    if (!(entrystr = eventlog_entry_encode (entry))) {
        log_err ("eventlog_entry_encode");
        goto error;
    }
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, key, entrystr) < 0) {
        log_err ("flux_kvs_txn_put");
        goto error;
    }
    rc = 0;
 error:
    /* on error, future destroyed via shell_input destroy */
    saved_errno = errno;
    json_decref (entry);
    free (entrystr);
    errno = saved_errno;
    return rc;
}

static void shell_input_kvs_init_completion (flux_future_t *f, void *arg)
{
    struct shell_input *in = arg;

    if (flux_future_get (f, NULL) < 0)
        /* failng to commit header is a fatal error.  Should be
         * cleaner in future. Issue #2378 */
        log_err_exit ("shell_input_kvs_init");
    flux_future_destroy (f);

    if (flux_shell_remove_completion_ref (in->shell, "input.kvs-init") < 0)
        log_err ("flux_shell_remove_completion_ref");

    if (in->stdin_type == FLUX_INPUT_TYPE_FILE)
        flux_watcher_start (in->stdin_file.w);
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
    if (shell_input_ready (in, txn) < 0)
        goto error;
    if (!(f = flux_kvs_commit (in->shell->h, NULL, 0, txn)))
        goto error;
    if (flux_future_then (f, -1, shell_input_kvs_init_completion, in) < 0)
        goto error;
    if (flux_shell_add_completion_ref (in->shell, "input.kvs-init") < 0) {
        log_err ("flux_shell_remove_completion_ref");
        goto error;
    }
    /* f memory responsibility of shell_input_kvs_init_completion()
     * callback */
    f = NULL;
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
                             "stdin", "base64",
                             "count",
                             "stdin", 1,
                             "options");
    if (!o) {
        errno = ENOMEM;
        goto error;
    }
    if (!in->shell->standalone) {
        if (shell_input_kvs_init (in, o) < 0)
            log_err ("shell_input_kvs_init");
    }
    rc = 0;
 error:
    json_decref (o);
    return rc;
}

static void shell_input_put_kvs_completion (flux_future_t *f, void *arg)
{
    struct shell_input *in = arg;

    if (flux_future_get (f, NULL) < 0)
        /* failng to write stdin to input is a fatal error.  Should be
         * cleaner in future. Issue #2378 */
        log_err_exit ("shell_input_put_kvs");
    flux_future_destroy (f);

    if (flux_shell_remove_completion_ref (in->shell, "input.kvs") < 0)
        log_err ("flux_shell_remove_completion_ref");
}

static int shell_input_put_kvs (struct shell_input *in,
                                void *buf,
                                int len,
                                bool eof)
{
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    json_t *entry = NULL;
    char *entrystr = NULL;
    json_t *context = NULL;
    int saved_errno;
    int rc = -1;

    if (!(context = ioencode ("stdin", in->stdin_file.rankstr, buf, len, eof)))
        goto error;
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
        log_err ("flux_shell_remove_completion_ref");
        goto error;
    }
    /* f memory responsibility of shell_input_put_kvs_completion()
     * callback */
    f = NULL;
    rc = 0;
 error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    json_decref (context);
    free (entrystr);
    json_decref (entry);
    flux_future_destroy (f);
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
        if (shell_input_put_kvs (in, buf, n, false) < 0)
            log_err_exit ("shell_input_put_kvs");
    }

    if (n < 0)
        log_err_exit ("shell_input_put_kvs");

    if (shell_input_put_kvs (in, NULL, 0, true) < 0)
        log_err_exit ("shell_input_put_kvs");

    flux_watcher_stop (w);
}

static int shell_input_type_file_setup (struct shell_input *in)
{
    struct shell_input_type_file *fp = &(in->stdin_file);

    if ((fp->fd = open (fp->path, O_RDONLY)) < 0) {
        log_err ("error opening input file '%s'", fp->path);
        return -1;
    }

    if (!(fp->w = flux_fd_watcher_create (in->shell->r, fp->fd,
                                          FLUX_POLLIN,
                                          shell_input_type_file_cb,
                                          in))) {
        log_err ("flux_fd_watcher_create");
        return -1;
    }

    if (in->shell->info->jobspec->task_count > 1) {
        if (asprintf (&fp->rankstr, "[0-%d]",
                      in->shell->info->jobspec->task_count) < 0) {
            log_err ("asprintf");
            return -1;
        }
    }
    else {
        if (!(fp->rankstr = strdup ("0"))) {
            log_err ("asprintf");
            return -1;
        }
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
    in->stdin_type = FLUX_INPUT_TYPE_NONE;
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
            if (shell_input_header (in) < 0)
                goto error;

            if (in->stdin_type == FLUX_INPUT_TYPE_FILE) {
                if (shell_input_type_file_setup (in) < 0)
                    goto error;
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

static void shell_task_input_kvs_input_cb (flux_future_t *f, void *arg)
{
    struct shell_task_input *task_input = arg;
    struct shell_task_input_kvs *kp = &(task_input->input_kvs);
    const char *entry;
    json_t *o;
    const char *name;
    json_t *context;

    /* Failure to read stdin in a fatal error.  Should be cleaner in
     * future.  Issue #2378 */

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        log_msg_exit ("flux_job_event_watch_get: %s",
                      future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        log_err_exit ("eventlog_entry_decode");
    if (eventlog_entry_parse (o, NULL, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (!strcmp (name, "header")) {
        /* Future: per-stream encoding */
        kp->input_header_parsed = true;
    }
    else if (!strcmp (name, "data")) {
        const char *rank = NULL;
        bool data_ok = false;
        if (!kp->input_header_parsed)
            log_msg_exit ("stream data read before header");
        if (iodecode (context, NULL, &rank, NULL, NULL, NULL) < 0)
            log_msg_exit ("malformed event context");
        if (!strcmp (rank, "all"))
            data_ok = true;
        else {
            struct idset *idset;
            if (!(idset = idset_decode (rank))) {
                log_err ("idset_decode '%s'", rank);
                goto out;
            }
            data_ok = idset_test (idset, task_input->task->rank);
            idset_destroy (idset);
        }
        if (data_ok) {
            const char *stream;
            char *data = NULL;
            int len;
            bool eof;
            if (kp->eof_reached) {
                log_msg_exit ("stream data after EOF");
                goto out;
            }
            if (iodecode (context, &stream, NULL, &data, &len, &eof) < 0)
                log_msg_exit ("malformed event context");
            if (len > 0) {
                if (flux_subprocess_write (task_input->task->proc,
                                           stream,
                                           data,
                                           len) < 0)
                    log_err_exit ("flux_subprocess_write");
            }
            if (eof) {
                if (flux_subprocess_close (task_input->task->proc, stream) < 0)
                    log_err_exit ("flux_subprocess_close");
                if (flux_job_event_watch_cancel (f) < 0)
                    log_err_exit ("flux_job_event_watch_cancel");
            }
            free (data);
        }
    }

out:
    json_decref (o);
    flux_future_reset (f);
    return;
done:
    flux_future_destroy (f);
    kp->input_f = NULL;
}

static void shell_task_input_kvs_exec_cb (flux_future_t *f, void *arg)
{
    struct shell_task_input *task_input = arg;
    struct shell_task_input_kvs *kp = &(task_input->input_kvs);
    flux_future_t *input_f = NULL;
    const char *entry;
    json_t *o;
    const char *name;

    /* Failure to read stdin in a fatal error.  Should be cleaner in
     * future.  Issue #2378 */

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        log_msg_exit ("flux_job_event_watch_get: %s",
                      future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        log_err_exit ("eventlog_entry_decode");
    if (eventlog_entry_parse (o, NULL, &name, NULL) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (!strcmp (name, "input-ready")) {
        if (!(input_f = flux_job_event_watch (task_input->in->shell->h,
                                              task_input->in->shell->info->jobid,
                                              "guest.input",
                                              0)))
            log_err_exit ("flux_job_event_watch");

        if (flux_future_then (input_f,
                              -1.,
                              shell_task_input_kvs_input_cb,
                              arg) < 0) {
            flux_future_destroy (input_f);
            log_err_exit ("flux_future_then");
        }

        kp->input_f = input_f;
    }

    json_decref (o);
    flux_future_reset (f);
    return;
 done:
    flux_future_destroy (f);
    kp->exec_f = NULL;
}

static int shell_task_input_kvs_setup (struct shell_task_input *task_input)
{
    /* Watch "guest.exec.eventlog" to determine when "guest.input" is ready */
    struct shell_task_input_kvs *kp = &(task_input->input_kvs);
    flux_future_t *f = NULL;
    int saved_errno;

    if (!(f = flux_job_event_watch (task_input->in->shell->h,
                                    task_input->in->shell->info->jobid,
                                    "guest.exec.eventlog",
                                    0))) {
        log_err ("flux_job_event_watch");
        goto error;
    }

    if (flux_future_then (f,
                          -1,
                          shell_task_input_kvs_exec_cb,
                          task_input) < 0) {
        log_err ("flux_future_then");
        goto error;
    }

    kp->exec_f = f;
    return 0;
 error:
    saved_errno = errno;
    flux_future_destroy (f);
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
    struct shell_task_input *task_input;
    flux_shell_task_t *task;

    if (!shell || !in || !(task = flux_shell_current_task (shell)))
        return -1;

    task_input = &(in->task_inputs[in->task_inputs_count]);
    task_input->in = in;
    task_input->task = task;

    if (task_input->type == FLUX_TASK_INPUT_KVS) {
        /* can't read stdin in standalone mode, no KVS to read from */
        if (!task_input->in->shell->standalone) {
            if (shell_task_input_kvs_setup (task_input) < 0)
                return -1;
        }
    }

    in->task_inputs_count++;
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
