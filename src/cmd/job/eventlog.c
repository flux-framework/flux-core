/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job eventlog, wait-event */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/formatter.h"
#include "ccan/str/str.h"
#include "common.h"

struct optparse_option eventlog_opts[] =  {
    { .name = "format", .key = 'f', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify output format: text, json",
    },
    { .name = "time-format", .key = 'T', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify time format: raw, iso, offset",
    },
    { .name = "human", .key = 'H', .has_arg = 0,
      .usage = "Display human-readable output. See also --color, --format, "
               "and --time-format.",
    },
    { .name = "color", .key = 'L', .has_arg = 2, .arginfo = "WHEN",
      .flags = OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG,
      .usage = "Colorize output when supported; WHEN can be 'always' "
               "(default if omitted), 'never', or 'auto' (default)."
    },
    { .name = "follow", .key = 'F', .has_arg = 0,
      .usage = "Follow events until job is inactive.",
    },
    { .name = "path", .key = 'p', .has_arg = 1, .arginfo = "PATH",
      .usage = "Specify alternate eventlog name or path suffix "
               "(e.g. \"exec\", \"output\", or \"guest.exec.eventlog\")",
    },
    OPTPARSE_TABLE_END
};

struct optparse_option wait_event_opts[] =  {
    { .name = "format", .key = 'f', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify output format: text, json",
    },
    { .name = "time-format", .key = 'T', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify time format: raw, iso, offset",
    },
    { .name = "human", .key = 'H', .has_arg = 0,
      .usage = "Display human-readable output. See also --color, --format, "
               "and --time-format.",
    },
    { .name = "timeout", .key = 't', .has_arg = 1, .arginfo = "DURATION",
      .usage = "timeout after DURATION",
    },
    { .name = "match-context", .key = 'm', .has_arg = 1, .arginfo = "KEY=VAL",
      .usage = "match key=val in context",
    },
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "COUNT",
      .usage = "required number of matches (default 1)",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Do not output matched event",
    },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Output all events before matched event",
    },
    { .name = "color", .key = 'L', .has_arg = 2, .arginfo = "WHEN",
      .flags = OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG,
      .usage = "Colorize output when supported; WHEN can be 'always' "
               "(default if omitted), 'never', or 'auto' (default)."
    },
    { .name = "path", .key = 'p', .has_arg = 1, .arginfo = "PATH",
      .usage = "Specify alternate eventlog name or path suffix "
               "(e.g. \"exec\", \"output\", or \"guest.exec.eventlog\")",
    },
    { .name = "waitcreate", .key = 'W', .has_arg = 0,
      .usage = "If path does not exist, wait for its creation",
    },
    OPTPARSE_TABLE_END
};

static int wait_event_run (optparse_t *p,
                           const char *jobid,
                           const char *wait_event,
                           const char *path,
                           const char *match_context,
                           int count,
                           double timeout,
                           bool waitcreate,
                           bool verbose,
                           bool quiet);

struct eventlog_ctx {
    optparse_t *p;
    const char *jobid;
    flux_jobid_t id;
    const char *path;
    struct eventlog_formatter *evf;
};

struct path_shortname {
    const char *name;
    const char *path;
};

/*  Set of shorthand names for common job eventlog paths:
 */
struct path_shortname eventlog_paths[] = {
    { "exec",   "guest.exec.eventlog" },
    { "output", "guest.output"        },
    { "input",  "guest.input"         },
    { NULL,     NULL                  },
};

const char *path_lookup (const char *name)
{
    const struct path_shortname *path = eventlog_paths;
    while (path->name) {
        if (streq (name, path->name))
            return path->path;
        path++;
    }
    return name;
}

static void formatter_parse_options (optparse_t *p,
                                     struct eventlog_formatter *evf)
{
    const char *format = optparse_get_str (p, "format", "text");
    const char *time_format = optparse_get_str (p, "time-format", "raw");
    int color = optparse_get_color (p, "color");

    if (optparse_hasopt (p, "human")) {
        format = "text",
        time_format = "human";
    }

    if (eventlog_formatter_set_format (evf, format) < 0)
        log_msg_exit ("invalid format type '%s'", format);
    if (eventlog_formatter_set_timestamp_format (evf, time_format) < 0)
        log_msg_exit ("invalid time-format type '%s'", time_format);
    (void) eventlog_formatter_set_color (evf, color);
}

static void eventlog_continuation (flux_future_t *f, void *arg)
{
    struct eventlog_ctx *ctx = arg;
    const char *s;
    json_t *a = NULL;
    size_t index;
    json_t *value;

    if (flux_rpc_get_unpack (f, "{s:s}", ctx->path, &s) < 0) {
        if (errno == ENOENT) {
            flux_future_destroy (f);
            if (streq (ctx->path, "eventlog"))
                log_msg_exit ("job %s not found", ctx->jobid);
            else
                log_msg_exit ("eventlog path %s not found", ctx->path);
        }
        else
            log_err_exit ("flux_job_eventlog_lookup_get");
    }

    if (!(a = eventlog_decode (s)))
        log_err_exit ("eventlog_decode");

    json_array_foreach (a, index, value) {
        flux_error_t error;
        if (eventlog_entry_dumpf (ctx->evf, stdout, &error, value) < 0)
            log_msg ("failed to print eventlog entry: %s", error.text);
    }

    fflush (stdout);
    json_decref (a);
    flux_future_destroy (f);
}

int cmd_eventlog (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    const char *topic = "job-info.lookup";
    struct eventlog_ctx ctx = {0};

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (argc - optindex != 1) {
        optparse_print_usage (p);
        exit (1);
    }

    ctx.jobid = argv[optindex++];

    if (optparse_hasopt (p, "follow"))
        return wait_event_run (p,
                               ctx.jobid,
                               "clean",
                               optparse_get_str (p, "path", "eventlog"),
                               NULL,
                               1,
                               -1.0,
                               true,
                               true,
                               false);

    ctx.id = parse_jobid (ctx.jobid);
    ctx.path = path_lookup (optparse_get_str (p, "path", "eventlog"));
    ctx.p = p;

    if (!(ctx.evf = eventlog_formatter_create ()))
        log_err_exit ("eventlog_formatter_create");
    formatter_parse_options (p, ctx.evf);

    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0,
                             "{s:I s:[s] s:i}",
                             "id", ctx.id,
                             "keys", ctx.path,
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");
    if (flux_future_then (f, -1., eventlog_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);
    eventlog_formatter_destroy (ctx.evf);
    return (0);
}

struct wait_event_ctx {
    optparse_t *p;
    const char *wait_event;
    const char *jobid;
    flux_jobid_t id;
    const char *path;
    bool got_event;
    struct eventlog_formatter *evf;
    char *context_key;
    char *context_value;
    int count;
    int match_count;
    bool verbose;
    bool quiet;
};

static bool wait_event_test_context (struct wait_event_ctx *ctx,
                                     json_t *context)
{
    const char *key;
    json_t *value;

    json_object_foreach (context, key, value) {
        if (streq (key, ctx->context_key)) {
            bool match;
            char *str = json_dumps (value, JSON_ENCODE_ANY|JSON_COMPACT);
            match = streq (str, ctx->context_value);
            free (str);

            /* Also try json_string_value() when value is a string:
             */
            if (!match
                && json_is_string (value)
                && streq (json_string_value (value), ctx->context_value))
                match = true;

            /*  Return immediately if a match was found:
             */
            if (match)
                return true;
        }
    }
    return false;
}

static bool wait_event_test (struct wait_event_ctx *ctx, json_t *event)
{
    double timestamp;
    const char *name;
    json_t *context = NULL;
    bool match = false;

    if (eventlog_entry_parse (event, &timestamp, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    /*  Ensure that timestamp zero is captured in eventlog formatter
     *  in case the entry is not processed in wait-event.
     */
    eventlog_formatter_update_t0 (ctx->evf, timestamp);

    if (streq (name, ctx->wait_event)) {
        if (ctx->context_key) {
            if (context)
                match = wait_event_test_context (ctx, context);
        }
        else
            match = true;
    }

    if (match && (++ctx->match_count) == ctx->count)
        return true;
    return false;
}

static void wait_event_continuation (flux_future_t *f, void *arg)
{
    struct wait_event_ctx *ctx = arg;
    json_t *o = NULL;
    const char *event;
    flux_error_t error;

    if (flux_rpc_get (f, NULL) < 0) {
        if (errno == ENOENT) {
            flux_future_destroy (f);
            if (streq (ctx->path, "eventlog"))
                log_msg_exit ("job %s not found", ctx->jobid);
            else
                log_msg_exit ("eventlog path %s not found", ctx->path);
        }
        else if (errno == ETIMEDOUT) {
            flux_future_destroy (f);
            log_msg_exit ("wait-event timeout on event '%s'",
                          ctx->wait_event);
        } else if (errno == ENODATA) {
            flux_future_destroy (f);
            if (!ctx->got_event)
                log_msg_exit ("event '%s' never received",
                              ctx->wait_event);
            return;
        }
        /* else fallthrough and have `flux_job_event_watch_get'
         * handle error */
    }

    if (flux_job_event_watch_get (f, &event) < 0)
        log_err_exit ("flux_job_event_watch_get");

    if (!(o = eventlog_entry_decode (event)))
        log_err_exit ("eventlog_entry_decode");

    if (wait_event_test (ctx, o)) {
        ctx->got_event = true;
        if (!ctx->quiet) {
            if (eventlog_entry_dumpf (ctx->evf, stdout, &error, o) < 0)
                log_err ("failed to print eventlog entry: %s", error.text);
            fflush (stdout);
        }
        if (flux_job_event_watch_cancel (f) < 0)
            log_err_exit ("flux_job_event_watch_cancel");
    } else if (ctx->verbose) {
        if (!ctx->got_event) {
            if (eventlog_entry_dumpf (ctx->evf, stdout, &error, o) < 0)
                log_err ("failed to print eventlog entry: %s", error.text);
            fflush (stdout);
        }
    }

    json_decref (o);

    flux_future_reset (f);
}

static int wait_event_run (optparse_t *p,
                           const char *jobid,
                           const char *wait_event,
                           const char *path,
                           const char *match_context,
                           int count,
                           double timeout,
                           bool waitcreate,
                           bool verbose,
                           bool quiet)
{
    flux_t *h;
    flux_future_t *f;
    struct wait_event_ctx ctx = {0};
    int flags = 0;

    ctx.p = p;
    ctx.jobid = jobid;
    ctx.id = parse_jobid (ctx.jobid);
    ctx.wait_event = wait_event;
    ctx.path = path_lookup (path);
    ctx.verbose = verbose;
    ctx.quiet = quiet;
    if ((ctx.count = count) <= 0)
        log_msg_exit ("count must be > 0");
    if (waitcreate)
        flags |= FLUX_JOB_EVENT_WATCH_WAITCREATE;

    if (!(ctx.evf = eventlog_formatter_create ()))
        log_err_exit ("eventlog_formatter_create");
    formatter_parse_options (p, ctx.evf);
    if (match_context) {
        ctx.context_key = xstrdup (match_context);
        ctx.context_value = strchr (ctx.context_key, '=');
        if (!ctx.context_value)
            log_msg_exit ("must specify a context test as key=value");
        *ctx.context_value++ = '\0';
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_job_event_watch (h, ctx.id, ctx.path, flags)))
        log_err_exit ("flux_job_event_watch");
    if (flux_future_then (f, timeout, wait_event_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    free (ctx.context_key);
    flux_close (h);
    eventlog_formatter_destroy (ctx.evf);
    return (0);
}

int cmd_wait_event (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    const char *jobid;
    const char *wait_event;

    if ((argc - optindex) != 2) {
        optparse_print_usage (p);
        exit (1);
    }
    jobid = argv[optindex++];
    wait_event = argv[optindex++];

    return wait_event_run (p,
                           jobid,
                           wait_event,
                           optparse_get_str (p, "path", "eventlog"),
                           optparse_get_str (p, "match-context", NULL),
                           optparse_get_int (p, "count", 1),
                           optparse_get_duration (p, "timeout", -1.0),
                           optparse_hasopt (p, "waitcreate"),
                           optparse_hasopt (p, "verbose"),
                           optparse_hasopt (p, "quiet"));
}

/* vi: ts=4 sw=4 expandtab
 */
