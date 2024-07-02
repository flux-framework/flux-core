/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job attach */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <jansson.h>
#include <sys/ioctl.h>
#include <signal.h>

#include <flux/core.h>
#include <flux/optparse.h>
#include <flux/idset.h>
#include <flux/taskmap.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/formatter.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libsubprocess/fbuf.h"
#include "src/common/libsubprocess/fbuf_watcher.h"
#include "src/common/libtaskmap/taskmap_private.h"

#include "src/common/libterminus/pty.h"
#include "src/common/libdebugged/debugged.h"
#include "src/common/libutil/monotime.h"
#include "ccan/str/str.h"

#include "job/common.h"
#include "job/mpir.h"

char *totalview_jobid = NULL;

int stdin_flags;

struct optparse_option attach_opts[] = {
    { .name = "show-events", .key = 'E', .has_arg = 0,
      .usage = "Show job events on stderr",
    },
    { .name = "show-exec", .key = 'X', .has_arg = 0,
      .usage = "Show exec events on stderr",
    },
    {
      .name = "show-status", .has_arg = 0,
      .usage = "Show job status line while pending",
    },
    { .name = "wait-event", .key = 'w', .has_arg = 1, .arginfo = "NAME",
      .usage = "Wait for event NAME before detaching from eventlog "
               "(default=finish)"
    },
    { .name = "label-io", .key = 'l', .has_arg = 0,
      .usage = "Label output by rank",
    },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Increase verbosity",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Suppress warnings written to stderr from flux-job",
    },
    { .name = "read-only", .key = 'r', .has_arg = 0,
      .usage = "Disable reading stdin and capturing signals",
    },
    { .name = "unbuffered", .key = 'u', .has_arg = 0,
      .usage = "Disable buffering of stdin",
    },
    { .name = "stdin-ranks", .key = 'i', .has_arg = 1, .arginfo = "RANKS",
      .usage = "Send standard input to only RANKS (default: all)"
    },
    { .name = "debug", .has_arg = 0,
      .usage = "Enable parallel debugger to attach to a running job",
    },
    { .name = "debug-emulate", .has_arg = 0, .flags = OPTPARSE_OPT_HIDDEN,
      .usage = "Set MPIR_being_debugged for testing",
    },
    OPTPARSE_TABLE_END
};

struct attach_ctx {
    flux_t *h;
    int exit_code;
    flux_jobid_t id;
    bool readonly;
    bool unbuffered;
    char *stdin_ranks;
    const char *jobid;
    const char *wait_event;
    flux_future_t *eventlog_f;
    flux_future_t *exec_eventlog_f;
    flux_future_t *output_f;
    flux_watcher_t *sigint_w;
    flux_watcher_t *sigtstp_w;
    flux_watcher_t *notify_timer;
    struct flux_pty_client *pty_client;
    int pty_capture;
    struct timespec t_sigint;
    flux_watcher_t *stdin_w;
    zlist_t *stdin_rpcs;
    bool stdin_data_sent;
    optparse_t *p;
    bool output_header_parsed;
    int leader_rank;
    char *service;
    double timestamp_zero;
    int eventlog_watch_count;
    bool statusline;
    char *last_event;
    bool fatal_exception;
    int last_queue_update;
    char *queue;
    bool queue_stopped;
};

void attach_completed_check (struct attach_ctx *ctx)
{
    /* stop all non-eventlog watchers and destroy all lingering
     * futures so we can exit the reactor */
    if (!ctx->eventlog_watch_count) {
        if (ctx->stdin_rpcs) {
            flux_future_t *f = zlist_pop (ctx->stdin_rpcs);
            while (f) {
                flux_future_destroy (f);
                zlist_remove (ctx->stdin_rpcs, f);
                f = zlist_pop (ctx->stdin_rpcs);
            }
        }
        flux_watcher_stop (ctx->sigint_w);
        flux_watcher_stop (ctx->sigtstp_w);
        flux_watcher_stop (ctx->stdin_w);
        flux_watcher_stop (ctx->notify_timer);
    }
}

/* Print eventlog entry to 'fp'.
 * Prefix and context may be NULL.
 */
void print_eventlog_entry (FILE *fp,
                           const char *prefix,
                           double timestamp,
                           const char *name,
                           json_t *context)
{
    char *context_s = NULL;

    if (context) {
        if (!(context_s = json_dumps (context, JSON_COMPACT)))
            log_err_exit ("%s: error re-encoding context", __func__);
    }
    fprintf (stderr, "%.3fs: %s%s%s%s%s\n",
             timestamp,
             prefix ? prefix : "",
             prefix ? "." : "",
             name,
             context_s ? " " : "",
             context_s ? context_s : "");
    free (context_s);
}

static void handle_output_data (struct attach_ctx *ctx, json_t *context)
{
    FILE *fp;
    const char *stream;
    const char *rank;
    char *data;
    int len;
    if (!ctx->output_header_parsed)
        log_msg_exit ("stream data read before header");
    if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0)
        log_msg_exit ("malformed event context");
    /*
     * If this process is attached to a pty (ctx->pty_client != NULL)
     *  and output corresponds to rank 0 and the interactive pty is being
     *  captured, then this data is a duplicate, so do nothing.
     */
    if (ctx->pty_client != NULL
        && streq (rank, "0")
        && ctx->pty_capture)
        goto out;
    if (streq (stream, "stdout"))
        fp = stdout;
    else
        fp = stderr;
    if (len > 0) {
        if (optparse_hasopt (ctx->p, "label-io"))
            fprintf (fp, "%s: ", rank);
        fwrite (data, len, 1, fp);
        /*  If attached to a pty, terminal is in raw mode so a carriage
         *  return will be necessary to return cursor to the start of line.
         */
        if (ctx->pty_client)
            fputc ('\r', fp);
        fflush (fp);
    }
out:
    free (data);
}

static void handle_output_redirect (struct attach_ctx *ctx, json_t *context)
{
    const char *stream = NULL;
    const char *rank = NULL;
    const char *path = NULL;
    if (!ctx->output_header_parsed)
        log_msg_exit ("stream redirect read before header");
    if (json_unpack (context, "{ s:s s:s s?s }",
                              "stream", &stream,
                              "rank", &rank,
                              "path", &path) < 0)
        log_msg_exit ("malformed redirect context");
    if (!optparse_hasopt (ctx->p, "quiet"))
        fprintf (stderr, "%s: %s redirected%s%s\n",
                         rank,
                         stream,
                         path ? " to " : "",
                         path ? path : "");
}

/*  Level prefix strings. Nominally, output log event 'level' integers
 *   are Internet RFC 5424 severity levels. In the context of flux-shell,
 *   the first 3 levels are equivalently "fatal" errors.
 */
static const char *levelstr[] = {
    "FATAL", "FATAL", "FATAL", "ERROR", " WARN", NULL, "DEBUG", "TRACE"
};

static void handle_output_log (struct attach_ctx *ctx,
                               double ts,
                               json_t *context)
{
    const char *msg = NULL;
    const char *file = NULL;
    const char *component = NULL;
    int rank = -1;
    int line = -1;
    int level = -1;
    json_error_t err;

    if (json_unpack_ex (context, &err, 0,
                        "{ s?i s:i s:s s?s s?s s?i }",
                        "rank", &rank,
                        "level", &level,
                        "message", &msg,
                        "component", &component,
                        "file", &file,
                        "line", &line) < 0) {
        log_err ("invalid log event in guest.output: %s", err.text);
        return;
    }
    if (!optparse_hasopt (ctx->p, "quiet")) {
        const char *label = levelstr [level];
        fprintf (stderr, "%.3fs: flux-shell", ts - ctx->timestamp_zero);
        if (rank >= 0)
            fprintf (stderr, "[%d]", rank);
        if (label)
            fprintf (stderr, ": %s", label);
        if (component)
            fprintf (stderr, ": %s", component);
        if (optparse_hasopt (ctx->p, "verbose") && file) {
            fprintf (stderr, ": %s", file);
            if (line > 0)
                fprintf (stderr, ":%d", line);
        }
        fprintf (stderr, ": %s\n", msg);
        /*  If attached to a pty, terminal is in raw mode so a carriage
         *  return will be necessary to return cursor to the start of line.
         */
        if (ctx->pty_client)
            fprintf (stderr, "\r");
    }
}

/* Handle an event in the guest.output eventlog.
 * This is a stream of responses, one response per event, terminated with
 * an ENODATA error response (or another error if something went wrong).
 * The first eventlog entry is a header; remaining entries are data,
 * redirect, or log messages.  Print each data entry to stdout/stderr,
 * with task/rank prefix if --label-io was specified.  For each redirect entry, print
 * information on paths to redirected locations if --quiet is not
 * specified.
 */
void attach_output_continuation (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;
    const char *entry;
    json_t *o;
    const char *name;
    double ts;
    json_t *context;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        if (errno == ENOENT) {
            log_msg ("No job output found");
            goto done;
        }
        log_msg_exit ("flux_job_event_watch_get: %s",
                      future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        log_err_exit ("eventlog_entry_decode");
    if (eventlog_entry_parse (o, &ts, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (streq (name, "header")) {
        /* Future: per-stream encoding */
        ctx->output_header_parsed = true;
    }
    else if (streq (name, "data")) {
        handle_output_data (ctx, context);
    }
    else if (streq (name, "redirect")) {
        handle_output_redirect (ctx, context);
    }
    else if (streq (name, "log")) {
        handle_output_log (ctx, ts, context);
    }

    json_decref (o);
    flux_future_reset (f);
    return;
done:
    flux_future_destroy (f);
    ctx->output_f = NULL;
    ctx->eventlog_watch_count--;
    attach_completed_check (ctx);
}

void attach_cancel_continuation (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0)
        log_msg ("cancel: %s", future_strerror (f, errno));
    flux_future_destroy (f);
}

/* Handle the user typing ctrl-C (SIGINT) and ctrl-Z (SIGTSTP).
 * If the user types ctrl-C twice within 2s, cancel the job.
 * If the user types ctrl-C then ctrl-Z within 2s, detach from the job.
 */
void attach_signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct attach_ctx *ctx = arg;
    flux_future_t *f;
    int signum = flux_signal_watcher_get_signum (w);

    if (signum == SIGINT) {
        if (monotime_since (ctx->t_sigint) > 2000) {
            monotime (&ctx->t_sigint);
            flux_watcher_start (ctx->sigtstp_w);
            log_msg ("one more ctrl-C within 2s to cancel or ctrl-Z to detach");
        }
        else {
            if (!(f = flux_job_cancel (ctx->h, ctx->id,
                                       "interrupted by ctrl-C")))
                log_err_exit ("flux_job_cancel");
            if (flux_future_then (f, -1, attach_cancel_continuation, NULL) < 0)
                log_err_exit ("flux_future_then");
        }
    }
    else if (signum == SIGTSTP) {
        if (monotime_since (ctx->t_sigint) <= 2000) {
            if (ctx->eventlog_f) {
                if (flux_job_event_watch_cancel (ctx->eventlog_f) < 0)
                    log_err_exit ("flux_job_event_watch_cancel");
            }
            if (ctx->exec_eventlog_f) {
                if (flux_job_event_watch_cancel (ctx->exec_eventlog_f) < 0)
                    log_err_exit ("flux_job_event_watch_cancel");
            }
            if (ctx->output_f) {
                if (flux_job_event_watch_cancel (ctx->output_f) < 0)
                    log_err_exit ("flux_job_event_watch_cancel");
            }
            log_msg ("detaching...");
        }
        else {
            flux_watcher_stop (ctx->sigtstp_w);
            log_msg ("one more ctrl-Z to suspend");
        }
    }
}

/* atexit handler
 * This is a good faith attempt to restore stdin flags to what they were
 * before we set O_NONBLOCK.
 */
void restore_stdin_flags (void)
{
    (void)fd_set_flags (STDIN_FILENO, stdin_flags);
}

static void attach_send_shell_completion (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;

    /* failing to write stdin to service is (generally speaking) a
     * fatal error */
    if (flux_future_get (f, NULL) < 0) {
        /* stdin may not be accepted for multiple reasons
         * - job has completed
         * - user requested stdin via file
         * - stdin stream already closed due to prior pipe in
         */
        if (errno == ENOSYS) {
            /* Only generate an error if an attempt to send stdin failed.
             */
            if (ctx->stdin_data_sent)
                log_msg_exit ("stdin not accepted by job");
        }
        else
            log_err_exit ("attach_send_shell");
    }
    flux_future_destroy (f);
    zlist_remove (ctx->stdin_rpcs, f);
}

static int attach_send_shell (struct attach_ctx *ctx,
                              const char *ranks,
                              const void *buf,
                              int len,
                              bool eof)
{
    json_t *context = NULL;
    char topic[1024];
    flux_future_t *f = NULL;
    int saved_errno;
    int rc = -1;

    snprintf (topic, sizeof (topic), "%s.stdin", ctx->service);
    if (!(context = ioencode ("stdin", ranks, buf, len, eof)))
        goto error;
    if (!(f = flux_rpc_pack (ctx->h, topic, ctx->leader_rank, 0, "O", context)))
        goto error;
    if (flux_future_then (f, -1, attach_send_shell_completion, ctx) < 0)
        goto error;
    if (zlist_append (ctx->stdin_rpcs, f) < 0)
        goto error;
    /* f memory now in hands of attach_send_shell_completion() or ctx->stdin_rpcs */
    f = NULL;
    rc = 0;
 error:
    saved_errno = errno;
    json_decref (context);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

/* Handle std input from user */
void attach_stdin_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    struct attach_ctx *ctx = arg;
    const char *ptr;
    int len;

    if (!(ptr = fbuf_read_watcher_get_data (w, &len)))
        log_err_exit ("fbuf_read_line on stdin");
    if (len > 0) {
        if (attach_send_shell (ctx, ctx->stdin_ranks, ptr, len, false) < 0)
            log_err_exit ("attach_send_shell");
        ctx->stdin_data_sent = true;
    }
    else {
        /* EOF */
        if (attach_send_shell (ctx, ctx->stdin_ranks, NULL, 0, true) < 0)
            log_err_exit ("attach_send_shell");
        flux_watcher_stop (ctx->stdin_w);
    }
}

/*  Start the guest.output eventlog watcher
 */
void attach_output_start (struct attach_ctx *ctx)
{
    if (ctx->output_f)
        return;

    if (!(ctx->output_f = flux_job_event_watch (ctx->h,
                                                ctx->id,
                                                "guest.output",
                                                0)))
        log_err_exit ("flux_job_event_watch");

    if (flux_future_then (ctx->output_f, -1.,
                          attach_output_continuation,
                          ctx) < 0)
        log_err_exit ("flux_future_then");

    ctx->eventlog_watch_count++;
}

static void valid_or_exit_for_debug (struct attach_ctx *ctx)
{
    flux_future_t *f = NULL;
    char *attrs = "[\"state\"]";
    flux_job_state_t state = FLUX_JOB_STATE_INACTIVE;

    if (!(f = flux_job_list_id (ctx->h, ctx->id, attrs)))
        log_err_exit ("flux_job_list_id");

    if (flux_rpc_get_unpack (f, "{s:{s:i}}", "job", "state", &state) < 0)
        log_err_exit ("Invalid job id (%s) for debugging", ctx->jobid);

    flux_future_destroy (f);

    if (state != FLUX_JOB_STATE_NEW
        && state != FLUX_JOB_STATE_DEPEND
        && state != FLUX_JOB_STATE_PRIORITY
        && state != FLUX_JOB_STATE_SCHED
        && state != FLUX_JOB_STATE_RUN) {
        log_msg_exit ("cannot debug job that has finished running");
    }

    return;
}

static void attach_setup_stdin (struct attach_ctx *ctx)
{
    flux_watcher_t *w;
    int flags = 0;

    if (ctx->readonly)
        return;

    if (!ctx->unbuffered)
        flags = FBUF_WATCHER_LINE_BUFFER;

    /* fbuf_read_watcher_create() requires O_NONBLOCK on
     * stdin */

    if ((stdin_flags = fd_set_nonblocking (STDIN_FILENO)) < 0)
        log_err_exit ("unable to set stdin nonblocking");
    if (atexit (restore_stdin_flags) != 0)
        log_err_exit ("atexit");

    w = fbuf_read_watcher_create (flux_get_reactor (ctx->h),
                                         STDIN_FILENO,
                                         1 << 20,
                                         attach_stdin_cb,
                                         flags,
                                         ctx);
    if (!w) {
        /* Users have reported rare occurrences of an EINVAL error
         * from fbuf_read_watcher_create(), the cause of which
         * is not understood (See issue #5175). In many cases, perhaps all,
         * stdin is not used by the job, so aborting `flux job attach`
         * is an unnecessary failure. Therefore, just ignore stdin when
         * errno is EINVAL here:
         */
        if (errno == EINVAL) {
            log_msg ("Warning: ignoring stdin: failed to create watcher");
            return;
        }
        log_err_exit ("fbuf_read_watcher_create");
    }

    if (!(ctx->stdin_rpcs = zlist_new ()))
        log_err_exit ("zlist_new");

    ctx->stdin_w = w;

    /*  Start stdin watcher only if --stdin-ranks=all (the default).
     *  Otherwise, the watcher will be started in close_stdin_ranks()
     *  after the idset of targeted ranks is adjusted based on the job
     *  taskmap.
     */
    if (streq (ctx->stdin_ranks, "all"))
        flux_watcher_start (ctx->stdin_w);
}

static void pty_client_exit_cb (struct flux_pty_client *c, void *arg)
{
    int status = 0;
    struct attach_ctx *ctx = arg;

    /*  If this client exited before the attach, then it must have been
     *   due to an RPC error. In that case, perhaps the remote pty has
     *   gone away, so fallback to attaching to KVS output eventlogs.
     */
    if (!flux_pty_client_attached (c)) {
        attach_setup_stdin (ctx);
        attach_output_start (ctx);
        return;
    }

    if (flux_pty_client_exit_status (c, &status) < 0)
        log_err ("Unable to get remote pty exit status");
    flux_pty_client_restore_terminal ();

    /*  Hm, should we force exit here?
     *  Need to differentiate between pty detach and normal exit.
     */
    exit (status == 0 ? 0 : 1);
}

static void f_logf (void *arg,
                    const char *file,
                    int line,
                    const char *func,
                    const char *subsys,
                    int level,
                    const char *fmt,
                    va_list ap)
{
    char buf [2048];
    int buflen = sizeof (buf);
    int n = vsnprintf (buf, buflen, fmt, ap);
    if (n >= sizeof (buf)) {
        buf[buflen - 1] = '\0';
        buf[buflen - 1] = '+';
    }
    log_msg ("%s:%d: %s: %s", file, line, func, buf);
}

static void attach_pty (struct attach_ctx *ctx, const char *pty_service)
{
    int n;
    char topic [128];
    int flags = FLUX_PTY_CLIENT_NOTIFY_ON_DETACH;

    if (!(ctx->pty_client = flux_pty_client_create ()))
        log_err_exit ("flux_pty_client_create");

    flux_pty_client_set_flags (ctx->pty_client, flags);
    flux_pty_client_set_log (ctx->pty_client, f_logf, NULL);

    n = snprintf (topic, sizeof (topic), "%s.%s", ctx->service, pty_service);
    if (n >= sizeof (topic))
        log_err_exit ("Failed to build pty service topic at %s.%s",
                      ctx->service, pty_service);

    /*  Attempt to attach to pty on rank 0 of this job.
     *  The attempt may fail if this job is not currently running.
     */
    if (flux_pty_client_attach (ctx->pty_client,
                                ctx->h,
                                ctx->leader_rank,
                                topic) < 0)
        log_err_exit ("failed attempting to attach to pty");

    if (flux_pty_client_notify_exit (ctx->pty_client,
                                     pty_client_exit_cb,
                                     ctx) < 0)
        log_err_exit ("flux_pty_client_notify_exit");
}

void handle_exec_log_msg (struct attach_ctx *ctx, double ts, json_t *context)
{
    const char *rank = NULL;
    const char *stream = NULL;
    const char *component = NULL;
    const char *data = NULL;
    size_t len = 0;
    json_error_t err;

    if (json_unpack_ex (context, &err, 0,
                        "{s:s s:s s:s s:s%}",
                        "rank", &rank,
                        "component", &component,
                        "stream", &stream,
                        "data", &data, &len) < 0) {
        log_msg ("exec.log event malformed: %s", err.text);
        return;
    }
    if (!optparse_hasopt (ctx->p, "quiet")) {
        fprintf (stderr,
                 "%.3fs: %s[%s]: %s: ",
                 ts - ctx->timestamp_zero,
                 component,
                 rank,
                 stream);
    }
    fwrite (data, len, 1, stderr);
}

static struct idset *all_taskids (const struct taskmap *map)
{
    struct idset *ids;
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    if (idset_range_set (ids, 0, taskmap_total_ntasks (map) - 1) < 0) {
        idset_destroy (ids);
        return NULL;
    }
    return ids;
}

static void adjust_stdin_ranks (struct attach_ctx *ctx,
                                struct idset *stdin_ranks,
                                struct idset *all_ranks)
{
    struct idset *isect = idset_intersect (all_ranks, stdin_ranks);
    if (!isect) {
        log_err ("failed to get intersection of stdin ranks and all taskids");
        return;
    }
    if (!idset_equal (stdin_ranks, isect)) {
        char *new = idset_encode (isect, IDSET_FLAG_RANGE);
        if (!new) {
            log_err ("unable to adjust stdin-ranks to job");
            goto out;
        }
        log_msg ("warning: adjusting --stdin-ranks from %s to %s",
                 ctx->stdin_ranks,
                 new);
        free (ctx->stdin_ranks);
        ctx->stdin_ranks = new;
    }
out:
    idset_destroy (isect);
}

static void handle_stdin_ranks (struct attach_ctx *ctx, json_t *context)
{
    flux_error_t error;
    json_t *omap;
    struct taskmap *map = NULL;
    struct idset *open = NULL;
    struct idset *to_close = NULL;
    struct idset *isect = NULL;
    char *ranks = NULL;

    if (streq (ctx->stdin_ranks, "all"))
        return;
    if (!(omap = json_object_get (context, "taskmap"))
        || !(map = taskmap_decode_json (omap, &error))
        || !(to_close = all_taskids (map))) {
        log_msg ("failed to process taskmap in shell.start event");
        goto out;
    }
    if (!(open = idset_decode (ctx->stdin_ranks))) {
        log_err ("failed to decode stdin ranks (%s)", ctx->stdin_ranks);
        goto out;
    }
    /* Ensure that stdin_ranks is a subset of all ranks
     */
    adjust_stdin_ranks (ctx, open, to_close);

    if (idset_subtract (to_close, open) < 0
        || !(ranks = idset_encode (to_close, IDSET_FLAG_RANGE))) {
        log_err ("unable to close stdin on non-targeted ranks");
        goto out;
    }
    if (attach_send_shell (ctx, ranks, NULL, 0, true) < 0)
        log_err ("failed to close stdin for %s", ranks);

    /*  Start watching stdin now that ctx->stdin_ranks has been
     *  validated.
     */
    flux_watcher_start (ctx->stdin_w);
out:
    taskmap_destroy (map);
    idset_destroy (open);
    idset_destroy (to_close);
    idset_destroy (isect);
    free (ranks);
}

/* Handle an event in the guest.exec eventlog.
 * This is a stream of responses, one response per event, terminated with
 * an ENODATA error response (or another error if something went wrong).
 * On the shell.init event, start watching the guest.output eventlog.
 * It is guaranteed to exist when guest.output is emitted.
 * If --show-exec was specified, print all events on stderr.
 */
void attach_exec_event_continuation (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;
    const char *entry;
    json_t *o;
    double timestamp;
    const char *name;
    json_t *context;
    const char *service;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        log_msg_exit ("flux_job_event_watch_get: %s",
                      future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        log_err_exit ("eventlog_entry_decode");
    if (eventlog_entry_parse (o, &timestamp, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (streq (name, "shell.init")) {
        const char *pty_service = NULL;
        if (json_unpack (context,
                         "{s:i s:s s?s s?i}",
                         "leader-rank",
                         &ctx->leader_rank,
                         "service",
                         &service,
                         "pty",
                         &pty_service,
                         "capture",
                         &ctx->pty_capture) < 0)
            log_err_exit ("error decoding shell.init context");
        if (!(ctx->service = strdup (service)))
            log_err_exit ("strdup service from shell.init");

        /*  If there is a pty service for this job, try to attach to it.
         *  The attach is asynchronous, and if it fails, we fall back to
         *   to kvs stdio handlers in the pty "exit callback".
         *
         *  If there is not a pty service, or the pty attach fails, continue
         *   to process normal stdio. (This may be because the job is
         *   already complete).
         */
        attach_output_start (ctx);
        if (pty_service) {
            if (ctx->readonly)
                log_msg_exit ("Cannot connect to pty in readonly mode");
            attach_pty (ctx, pty_service);
        }
        else
            attach_setup_stdin (ctx);
    } else if (streq (name, "shell.start")) {
        if (MPIR_being_debugged) {
            int stop_tasks_in_exec = 0;
            if (json_unpack (context, "{s?b}", "sync", &stop_tasks_in_exec) < 0)
                log_err ("error decoding shell.start context");
            mpir_setup_interface (ctx->h,
                                  ctx->id,
                                  optparse_hasopt (ctx->p, "debug-emulate"),
                                  stop_tasks_in_exec,
                                  ctx->leader_rank,
                                  ctx->service);
        }
        handle_stdin_ranks (ctx, context);
    }
    else if (streq (name, "log")) {
        handle_exec_log_msg (ctx, timestamp, context);
    }

    /*  If job is complete, and we haven't started watching
     *   output eventlog, then start now in case shell.init event
     *   was never emitted (failure in initialization)
     */
    if (streq (name, "complete") && !ctx->output_f)
        attach_output_start (ctx);

    if (optparse_hasopt (ctx->p, "show-exec")
        && !streq (name, "log")) {
        print_eventlog_entry (stderr,
                              "exec",
                              timestamp - ctx->timestamp_zero,
                              name,
                              context);
    }

    json_decref (o);
    flux_future_reset (f);
    return;
done:
    flux_future_destroy (f);
    ctx->exec_eventlog_f = NULL;
    ctx->eventlog_watch_count--;
    attach_completed_check (ctx);
}

struct job_event_notifications {
    const char *event;
    const char *msg;
    int count;
};

static struct job_event_notifications attach_notifications[] = {
    { "validate",
      "resolving dependencies",
      0,
    },
    { "depend",
      "waiting for priority assignment",
      0,
    },
    { "priority",
      "waiting for resources",
      0,
    },
    { "alloc",
      "starting",
      0,
    },
    { "prolog-start",
      "waiting for job prolog",
      0,
    },
    { "prolog-finish",
      "starting",
      0,
    },
    { "start",
      "started",
      0,
    },
    { "exception",
      "canceling due to exception",
      0,
    },
    { NULL, NULL, 0},
};

static void queue_status_cb (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;
    int start;
    if (flux_rpc_get_unpack (f, "{s:b}", "start", &start) == 0)
        ctx->queue_stopped = !start;
    flux_future_destroy (f);
}

static void fetch_queue_status (struct attach_ctx *ctx)
{
    flux_future_t *f = NULL;

    /*  We don't yet have the queue, do nothing
     */
    if (!ctx->queue)
        return;

    if (streq (ctx->queue, "default"))
        f = flux_rpc (ctx->h, "job-manager.queue-status", "{}", 0, 0);
    else
        f = flux_rpc_pack (ctx->h,
                           "job-manager.queue-status",
                            0,
                            0,
                            "{s:s?}",
                            "name", ctx->queue);
    if (f && flux_future_then (f, -1., queue_status_cb, ctx) < 0)
        flux_future_destroy (f);
}

static void job_queue_cb (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;
    const char *queue = "default";
    if (flux_rpc_get_unpack (f, "{s:{s?s}}", "job", "queue", &queue) == 0)
        ctx->queue = strdup (queue);
    flux_future_destroy (f);
}

static void fetch_job_queue (struct attach_ctx *ctx)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (ctx->h,
                             "job-list.list-id",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:[s]}",
                             "id", ctx->id,
                             "attrs", "queue"))
        || flux_future_then (f, -1., job_queue_cb, ctx) < 0)
        flux_future_destroy (f);
}

static const char *job_event_notify_string (const char *name)
{
    struct job_event_notifications *t = attach_notifications;
    while (t->event) {
        if (streq (t->event, name)) {
            /*  Special handling for prolog-start and prolog-finish:
             *  prolog-start adds a reference to prolog-finish, and
             *  prolog-finish decrements its reference by 1. Only
             *  print 'starting' if prolog-finish refcount is <= 0.
             */
            if (streq (name, "prolog-start"))
                (t+1)->count++;
            else if (streq (name, "prolog-finish")
                    && --t->count > 0)
                    t--;
            return t->msg;
        }
        t++;
    }
    return NULL;
}

static void attach_notify (struct attach_ctx *ctx,
                           const char *event_name,
                           double ts)
{
    char buf[64];
    const char *msg;

    if (!event_name)
        return;

    /* job_event_notify_string must be called for all events even if
     * the statusline is not active for prolog-start/finish refcounting.
     */
    msg = job_event_notify_string (event_name);
    if (msg
        && ctx->statusline
        && !ctx->fatal_exception) {
        int dt = ts - ctx->timestamp_zero;
        int width = 80;
        struct winsize w;

        if (streq (msg, "waiting for resources")) {
            /*  Fetch job queue if not already available so queue status
             *  can be checked in case allocations are stopped:
             */
            if (!ctx->queue)
                fetch_job_queue (ctx);
            else {
                /*  Check queue status, only check again every ~10s
                 */
                if (ctx->last_queue_update <= 0
                    || (dt - ctx->last_queue_update >= 10)) {
                    ctx->last_queue_update = dt;
                    fetch_queue_status (ctx);
                }
            }

            /*  Amend status if queue is stopped:
             */
            if (ctx->queue_stopped) {
                if (snprintf (buf,
                              sizeof (buf),
                              "%s (%s queue stopped)",
                              msg,
                              ctx->queue) < sizeof (buf))
                    msg = buf;
            }
        }

        /* Adjust width of status so timer is right justified:
         */
        if (ioctl(0, TIOCGWINSZ, &w) == 0)
            width = w.ws_col;
        width -= 10 + strlen (ctx->jobid) + 10;

        fprintf (stderr,
                 "\rflux-job: %s %-*s %02d:%02d:%02d\r",
                 ctx->jobid,
                 width,
                 msg,
                 dt/3600,
                 (dt/60) % 60,
                 dt % 60);
    }
    if (streq (event_name, "start")
        || streq (event_name, "clean")) {
        if (ctx->statusline) {
            fprintf (stderr, "\n");
            ctx->statusline = false;
        }
        flux_watcher_stop (ctx->notify_timer);
    }
    if (!ctx->last_event || !streq (event_name, ctx->last_event)) {
        free (ctx->last_event);
        ctx->last_event = strdup (event_name);
    }
}

void attach_notify_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct attach_ctx *ctx = arg;
    ctx->statusline = true;
    attach_notify (ctx, ctx->last_event, flux_reactor_time ());
}


/* Handle an event in the main job eventlog.
 * This is a stream of responses, one response per event, terminated with
 * an ENODATA error response (or another error if something went wrong).
 * If a fatal exception event occurs, print it on stderr.
 * If --show-events was specified, print all events on stderr.
 * If submit event occurs, begin watching guest.exec.eventlog.
 * If finish event occurs, capture ctx->exit code.
 */
void attach_event_continuation (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;
    const char *entry;
    json_t *o = NULL;
    double timestamp;
    const char *name;
    json_t *context;
    int status;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        if (errno == ENOENT)
            log_msg_exit ("Failed to attach to %s: No such job", ctx->jobid);
        if (errno == EPERM)
            log_msg_exit ("Failed to attach to %s: that is not your job",
                          ctx->jobid);
        log_msg_exit ("flux_job_event_watch_get: %s",
                      future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        log_err_exit ("eventlog_entry_decode");
    if (eventlog_entry_parse (o, &timestamp, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (ctx->timestamp_zero == 0.)
        ctx->timestamp_zero = timestamp;

    if (streq (name, "exception")) {
        const char *type;
        int severity;
        const char *note;

        if (json_unpack (context, "{s:s s:i s:s}",
                         "type", &type,
                         "severity", &severity,
                         "note", &note) < 0)
            log_err_exit ("error decoding exception context");
        fprintf (stderr, "%.3fs: job.exception type=%s severity=%d %s\n",
                         timestamp - ctx->timestamp_zero,
                         type,
                         severity,
                         note);

        ctx->fatal_exception = (severity == 0);

        /*  If this job has an interactive pty and the pty is not yet attached,
         *   destroy the pty to avoid a potential hang attempting to connect
         *   to job pty that will never exist.
         */
        if (severity == 0
            && ctx->pty_client
            && !flux_pty_client_attached (ctx->pty_client)) {
            flux_pty_client_destroy (ctx->pty_client);
            ctx->pty_client = NULL;
        }
    }
    else if (streq (name, "submit")) {
        if (!(ctx->exec_eventlog_f = flux_job_event_watch (ctx->h,
                                                           ctx->id,
                                                           "guest.exec.eventlog",
                                                           0)))
            log_err_exit ("flux_job_event_watch");
        if (flux_future_then (ctx->exec_eventlog_f,
                              -1,
                              attach_exec_event_continuation,
                              ctx) < 0)
            log_err_exit ("flux_future_then");

        ctx->eventlog_watch_count++;
    }
    else {
        if (streq (name, "finish")) {
            flux_error_t error;
            if (json_unpack (context, "{s:i}", "status", &status) < 0)
                log_err_exit ("error decoding finish context");
            ctx->exit_code = flux_job_waitstatus_to_exitcode (status, &error);
            if (ctx->exit_code != 0)
                log_msg ("%s", error.text);
        }
    }

    if (optparse_hasopt (ctx->p, "show-events")
        && !streq (name, "exception")) {
        print_eventlog_entry (stderr,
                              "job",
                              timestamp - ctx->timestamp_zero,
                              name,
                              context);
    }

    attach_notify (ctx, name, timestamp);

    if (streq (name, ctx->wait_event)) {
        flux_job_event_watch_cancel (f);
        goto done;
    }

    json_decref (o);
    flux_future_reset (f);
    return;
done:
    json_decref (o);
    flux_future_destroy (f);
    ctx->eventlog_f = NULL;
    ctx->eventlog_watch_count--;
    attach_completed_check (ctx);
}

static char *get_stdin_ranks (optparse_t *p)
{
    const char *value = optparse_get_str (p, "stdin-ranks", "all");
    if (!streq (value, "all")) {
        struct idset *ids;
        if (!(ids = idset_decode (value)))
            log_err_exit ("Invalid value '%s' for --stdin-ranks", value);
        idset_destroy (ids);
    }
    return strdup (value);
}

static void initialize_attach_statusline (struct attach_ctx *ctx,
                                          flux_reactor_t *r)
{
    /*  Never show status line if `FLUX_ATTACH_INTERACTIVE` is set
     */
    if (getenv ("FLUX_ATTACH_NONINTERACTIVE"))
        return;

    /*  Only enable the statusline if it is was explicitly requested via
     *  --show status, or it is reasonably probable that flux-job attach
     *  is being used interactively -- i.e. all of stdin, stdout, and stderr
     *  are connected to a tty.
     */
    if ((ctx->statusline = optparse_hasopt (ctx->p, "show-status"))
        || (!optparse_hasopt (ctx->p, "show-events")
            && isatty (STDIN_FILENO)
            && isatty (STDOUT_FILENO)
            && isatty (STDERR_FILENO))) {
        /*
         * If flux-job is running interactively, and the job has not
         * started within 2s, then display a status line notifying the
         * user of the job's status. The timer repeats every second after
         * the initial callback to update a clock displayed on the rhs of
         * the status line.
         *
         * The timer is automatically stopped after the 'start' or 'clean'
         * event.
         */
        ctx->notify_timer = flux_timer_watcher_create (r,
                                                       ctx->statusline ? 0.:2.,
                                                       1.,
                                                       attach_notify_cb,
                                                       ctx);
        if (!ctx->notify_timer)
            log_err ("Failed to start notification timer");
        flux_watcher_start (ctx->notify_timer);
    }
}

int cmd_attach (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_reactor_t *r;
    struct attach_ctx ctx;

    memset (&ctx, 0, sizeof (ctx));
    ctx.exit_code = 1;

    if (argc - optindex != 1) {
        optparse_print_usage (p);
        exit (1);
    }
    ctx.jobid = argv[optindex++];
    ctx.id = parse_jobid (ctx.jobid);
    ctx.p = p;
    ctx.readonly = optparse_hasopt (p, "read-only");
    ctx.unbuffered = optparse_hasopt (p, "unbuffered");

    if (optparse_hasopt (p, "stdin-ranks") && ctx.readonly)
        log_msg_exit ("Do not use --stdin-ranks with --read-only");
    ctx.stdin_ranks = get_stdin_ranks (p);

    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(r = flux_get_reactor (ctx.h)))
        log_err_exit ("flux_get_reactor");

    /*  Check for the event name that attach should wait for in the
     *   main job eventlog. The default is the "finish" event.
     *   If the event never appears in the eventlog, flux-job attach
     *   will still exit after the 'clean' event, since the job-info
     *   module reponds with ENODATA after the final event, which by
     *   definition is "clean".
     */
    ctx.wait_event = optparse_get_str (p, "wait-event", "finish");

    if (optparse_hasopt (ctx.p, "debug")
        || optparse_hasopt (ctx.p, "debug-emulate")) {
        MPIR_being_debugged = 1;
    }
    if (MPIR_being_debugged) {
        int verbose = optparse_getopt (p, "verbose", NULL);
        valid_or_exit_for_debug (&ctx);
        totalview_jobid = xasprintf ("%ju", (uintmax_t)ctx.id);
        if (verbose > 1)
            log_msg ("totalview_jobid=%s", totalview_jobid);
    }

    if (!(ctx.eventlog_f = flux_job_event_watch (ctx.h,
                                                 ctx.id,
                                                 "eventlog",
                                                 0)))
        log_err_exit ("flux_job_event_watch");
    if (flux_future_then (ctx.eventlog_f,
                          -1,
                          attach_event_continuation,
                          &ctx) < 0)
        log_err_exit ("flux_future_then");

    ctx.eventlog_watch_count++;

    if (!ctx.readonly) {
        ctx.sigint_w = flux_signal_watcher_create (r,
                                                   SIGINT,
                                                   attach_signal_cb,
                                                   &ctx);
        ctx.sigtstp_w = flux_signal_watcher_create (r,
                                                    SIGTSTP,
                                                    attach_signal_cb,
                                                    &ctx);
        if (!ctx.sigint_w || !ctx.sigtstp_w)
            log_err_exit ("flux_signal_watcher_create");
        flux_watcher_start (ctx.sigint_w);
    }

    initialize_attach_statusline (&ctx, r);

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    zlist_destroy (&(ctx.stdin_rpcs));
    flux_watcher_destroy (ctx.sigint_w);
    flux_watcher_destroy (ctx.sigtstp_w);
    flux_watcher_destroy (ctx.stdin_w);
    flux_watcher_destroy (ctx.notify_timer);
    flux_pty_client_destroy (ctx.pty_client);
    flux_close (ctx.h);
    free (ctx.service);
    free (totalview_jobid);
    free (ctx.last_event);
    free (ctx.queue);
    free (ctx.stdin_ranks);

    if (ctx.fatal_exception && ctx.exit_code == 0)
        ctx.exit_code = 1;

    return ctx.exit_code;
}

/* vi: ts=4 sw=4 expandtab
 */
