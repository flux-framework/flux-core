/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* "plumbing" commands (see git(1)) for Flux job management */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <jansson.h>
#include <argz.h>
#include <flux/core.h>
#include <flux/optparse.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libjob/job.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libeventlog/eventlog.h"

int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_submit (optparse_t *p, int argc, char **argv);
int cmd_attach (optparse_t *p, int argc, char **argv);
int cmd_id (optparse_t *p, int argc, char **argv);
int cmd_cancel (optparse_t *p, int argc, char **argv);
int cmd_raise (optparse_t *p, int argc, char **argv);
int cmd_priority (optparse_t *p, int argc, char **argv);
int cmd_eventlog (optparse_t *p, int argc, char **argv);
int cmd_wait_event (optparse_t *p, int argc, char **argv);
int cmd_info (optparse_t *p, int argc, char **argv);
int cmd_drain (optparse_t *p, int argc, char **argv);
int cmd_undrain (optparse_t *p, int argc, char **argv);

static struct optparse_option global_opts[] =  {
    OPTPARSE_TABLE_END
};

static struct optparse_option list_opts[] =  {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Limit output to N jobs",
    },
    { .name = "suppress-header", .key = 's', .has_arg = 0,
      .usage = "Suppress printing of header line",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option raise_opts[] =  {
    { .name = "severity", .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set exception severity [0-7] (default=0)",
    },
    { .name = "type", .key = 't', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Set exception type (default=cancel)",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option submit_opts[] =  {
    { .name = "priority", .key = 'p', .has_arg = 1, .arginfo = "N",
      .usage = "Set job priority (0-31, default=16)",
    },
    { .name = "flags", .key = 'f', .has_arg = 3,
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set submit comma-separated flags (e.g. debug)",
    },
#if HAVE_FLUX_SECURITY
    { .name = "security-config", .key = 'c', .has_arg = 1, .arginfo = "pattern",
      .usage = "Use non-default security config glob",
    },
    { .name = "sign-type", .key = 's', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Use non-default mechanism type to sign J",
    },
#endif
    OPTPARSE_TABLE_END
};

static struct optparse_option attach_opts[] =  {
    { .name = "show-events", .key = 'E', .has_arg = 0,
      .usage = "Show job events on stderr",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option id_opts[] =  {
    { .name = "from", .key = 'f', .has_arg = 1,
      .arginfo = "dec|kvs|words",
      .usage = "Convert jobid from specified form",
    },
    { .name = "to", .key = 't', .has_arg = 1,
      .arginfo = "dec|kvs|words",
      .usage = "Convert jobid to specified form",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option eventlog_opts[] =  {
    { .name = "format", .key = 'f', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify output format: text, json",
    },
    { .name = "time-format", .key = 'T', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify time format: raw, iso, offset",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option wait_event_opts[] =  {
    { .name = "format", .key = 'f', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify output format: text, json",
    },
    { .name = "time-format", .key = 'T', .has_arg = 1, .arginfo = "FORMAT",
      .usage = "Specify time format: raw, iso, offset",
    },
    { .name = "timeout", .key = 't', .has_arg = 1, .arginfo = "DURATION",
      .usage = "timeout after DURATION",
    },
    { .name = "match-context", .key = 'm', .has_arg = 1, .arginfo = "KEY=VAL",
      .usage = "match key=val in context",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Do not output matched event",
    },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Output all events before matched event",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option drain_opts[] =  {
    { .name = "timeout", .key = 't', .has_arg = 1, .arginfo = "DURATION",
      .usage = "timeout after DURATION",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "list",
      "[OPTIONS]",
      "List active jobs",
      cmd_list,
      0,
      list_opts
    },
    { "priority",
      "[OPTIONS] id priority",
      "Set job priority",
      cmd_priority,
      0,
      NULL,
    },
    { "cancel",
      "[OPTIONS] id [message ...]",
      "Cancel a job",
      cmd_cancel,
      0,
      NULL,
    },
    { "raise",
      "[OPTIONS] id [message ...]",
      "Raise exception for job",
      cmd_raise,
      0,
      raise_opts,
    },
    { "attach",
      "[OPTIONS] id",
      "Interactively attach to job",
      cmd_attach,
      0,
      attach_opts,
    },
    { "submit",
      "[OPTIONS] [jobspec]",
      "Run job",
      cmd_submit,
      0,
      submit_opts
    },
    { "id",
      "[OPTIONS] [id ...]",
      "Convert jobid(s) to another form",
      cmd_id,
      0,
      id_opts
    },
    { "eventlog",
      "[-f text|json] [-T raw|iso|offset] id",
      "Display eventlog for a job",
      cmd_eventlog,
      0,
      eventlog_opts
    },
    { "wait-event",
      "[-f text|json] [-T raw|iso|offset] [-t seconds] [-m key=val] id event",
      "Wait for an event ",
      cmd_wait_event,
      0,
      wait_event_opts
    },
    { "info",
      "id key ...",
      "Display info for a job",
      cmd_info,
      0,
      NULL
    },
    { "drain",
      "[-t seconds]",
      "Disable job submissions and wait for queue to empty.",
      cmd_drain,
      0,
      drain_opts
    },
    { "undrain",
      NULL,
      "Re-enable job submissions after drain.",
      cmd_undrain,
      0,
      NULL
    },
    OPTPARSE_SUBCMD_END
};

int usage (optparse_t *p, struct optparse_option *o, const char *optarg)
{
    struct optparse_subcommand *s;
    optparse_print_usage (p);
    fprintf (stderr, "\n");
    fprintf (stderr, "Common commands from flux-job:\n");
    s = subcommands;
    while (s->name) {
        fprintf (stderr, "   %-15s %s\n", s->name, s->doc);
        s++;
    }
    exit (1);
}

int main (int argc, char *argv[])
{
    char *cmdusage = "[OPTIONS] COMMAND ARGS";
    optparse_t *p;
    int optindex;
    int exitval;

    log_init ("flux-job");

    p = optparse_create ("flux-job");

    if (optparse_add_option_table (p, global_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table() failed");

    /* Override help option for our own */
    if (optparse_set (p, OPTPARSE_USAGE, cmdusage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (USAGE)");

    /* Override --help callback in favor of our own above */
    if (optparse_set (p, OPTPARSE_OPTION_CB, "help", usage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set() failed");

    /* Don't print internal subcommands, we do it ourselves */
    if (optparse_set (p, OPTPARSE_PRINT_SUBCMDS, 0) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (PRINT_SUBCMDS)");

    if (optparse_reg_subcommands (p, subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_reg_subcommands");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    if ((argc - optindex == 0)
        || !optparse_get_subcommand (p, argv[optindex])) {
        usage (p, NULL, NULL);
        exit (1);
    }

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

/* Parse a free argument 's', expected to be a 64-bit unsigned.
 * On error, exit complaining about parsing 'name'.
 */
static unsigned long long parse_arg_unsigned (const char *s, const char *name)
{
    unsigned long long i;
    char *endptr;

    errno = 0;
    i = strtoull (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        log_msg_exit ("error parsing %s", name);
    return i;
}

/* Parse free arguments into a space-delimited message.
 * On error, exit complaning about parsing 'name'.
 * Caller must free the resulting string
 */
static char *parse_arg_message (char **argv, const char *name)
{
    char *argz = NULL;
    size_t argz_len = 0;
    error_t e;

    if ((e = argz_create (argv, &argz, &argz_len)) != 0)
        log_errn_exit (e, "error parsing %s", name);
    argz_stringify (argz, argz_len, ' ');
    return argz;
}

int cmd_priority (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_future_t *f;
    int priority;
    flux_jobid_t id;

    if (optindex != argc - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    id = parse_arg_unsigned (argv[optindex++], "jobid");
    priority = parse_arg_unsigned (argv[optindex++], "priority");

    if (!(f = flux_job_set_priority (h, id, priority)))
        log_err_exit ("flux_job_set_priority");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("%llu: %s", (unsigned long long)id,
                      future_strerror (f, errno));
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

int cmd_raise (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int severity = optparse_get_int (p, "severity", 0);
    const char *type = optparse_get_str (p, "type", "cancel");
    flux_t *h;
    flux_jobid_t id;
    char *note = NULL;
    flux_future_t *f;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    id = parse_arg_unsigned (argv[optindex++], "jobid");
    if (optindex < argc)
        note = parse_arg_message (argv + optindex, "message");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_job_raise (h, id, type, severity, note)))
        log_err_exit ("flux_job_raise");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("%llu: %s", (unsigned long long)id,
                      future_strerror (f, errno));
    flux_future_destroy (f);
    flux_close (h);
    free (note);
    return 0;
}

int cmd_cancel (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_jobid_t id;
    char *note = NULL;
    flux_future_t *f;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    id = parse_arg_unsigned (argv[optindex++], "jobid");
    if (optindex < argc)
        note = parse_arg_message (argv + optindex, "message");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_job_cancel (h, id, note)))
        log_err_exit ("flux_job_cancel");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("%llu: %s", (unsigned long long)id,
                      future_strerror (f, errno));
    flux_future_destroy (f);
    flux_close (h);
    free (note);
    return 0;
}

/* convert floating point timestamp (UNIX epoch, UTC) to ISO 8601 string,
 * with second precision
 */
static int iso_timestr (double timestamp, char *buf, size_t size)
{
    time_t sec = timestamp;
    struct tm tm;

    if (!gmtime_r (&sec, &tm))
        return -1;
    if (strftime (buf, size, "%FT%TZ", &tm) == 0)
        return -1;
    return 0;
}

int cmd_list (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int max_entries = optparse_get_int (p, "count", 0);
    char *attrs = "[\"id\",\"userid\",\"priority\",\"t_submit\",\"state\"]";
    flux_t *h;
    flux_future_t *f;
    json_t *jobs;
    size_t index;
    json_t *value;

    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_job_list (h, max_entries, attrs)))
        log_err_exit ("flux_job_list");
    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0)
        log_err_exit ("flux_job_list");
    if (!optparse_hasopt (p, "suppress-header"))
        printf ("%s\t\t%s\t%s\t%s\t%s\n",
                "JOBID", "STATE", "USERID", "PRI", "T_SUBMIT");
    json_array_foreach (jobs, index, value) {
        flux_jobid_t id;
        int priority;
        uint32_t userid;
        double t_submit;
        char timestr[80];
        flux_job_state_t state;

        if (json_unpack (value, "{s:I s:i s:i s:f s:i}",
                                "id", &id,
                                "priority", &priority,
                                "userid", &userid,
                                "t_submit", &t_submit,
                                "state", &state) < 0)
            log_msg_exit ("error parsing job data");
        if (iso_timestr (t_submit, timestr, sizeof (timestr)) < 0)
            log_err_exit ("time conversion error");
        printf ("%llu\t%s\t%lu\t%d\t%s\n", (unsigned long long)id,
                                       flux_job_statetostr (state, true),
                                       (unsigned long)userid,
                                       priority,
                                       timestr);
    }
    flux_future_destroy (f);
    flux_close (h);

   return (0);
}

/* Read entire file 'name' ("-" for stdin).  Exit program on error.
 */
size_t read_jobspec (const char *name, void **bufp)
{
    int fd;
    ssize_t size;
    void *buf;

    if (!strcmp (name, "-"))
        fd = STDIN_FILENO;
    else {
        if ((fd = open (name, O_RDONLY)) < 0)
            log_err_exit ("%s", name);
    }
    if ((size = read_all (fd, &buf)) < 0)
        log_err_exit ("%s", name);
    if (fd != STDIN_FILENO)
        (void)close (fd);
    *bufp = buf;
    return size;
}

int cmd_submit (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
#if HAVE_FLUX_SECURITY
    flux_security_t *sec = NULL;
    const char *sec_config;
    const char *sign_type;
#endif
    int flags = 0;
    void *jobspec;
    int jobspecsz;
    const char *J = NULL;
    int priority;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    flux_jobid_t id;
    const char *input = "-";

    if (optindex != argc - 1 && optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < argc)
        input = argv[optindex++];
    if (optparse_hasopt (p, "flags")) {
        const char *name;
        while ((name = optparse_getopt_next (p, "flags"))) {
            if (!strcmp (name, "debug"))
                flags |= FLUX_JOB_DEBUG;
            else
                log_msg_exit ("unknown flag: %s", name);
        }
    }
#if HAVE_FLUX_SECURITY
    /* If any non-default security options are specified, create security
     * context so jobspec can be pre-signed before submission.
     */
    if (optparse_hasopt (p, "security-config")
                            || optparse_hasopt (p, "sign-type")) {
        sec_config = optparse_get_str (p, "security-config", NULL);
        if (!(sec = flux_security_create (0)))
            log_err_exit ("security");
        if (flux_security_configure (sec, sec_config) < 0)
            log_err_exit ("security config %s", flux_security_last_error (sec));
        sign_type = optparse_get_str (p, "sign-type", NULL);
    }
#endif
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    jobspecsz = read_jobspec (input, &jobspec);
    assert (((char *)jobspec)[jobspecsz] == '\0');
    if (jobspecsz == 0)
        log_msg_exit ("required jobspec is empty");
    priority = optparse_get_int (p, "priority", FLUX_JOB_PRIORITY_DEFAULT);

#if HAVE_FLUX_SECURITY
    if (sec) {
        if (!(J = flux_sign_wrap (sec, jobspec, jobspecsz, sign_type, 0)))
            log_err_exit ("flux_sign_wrap: %s", flux_security_last_error (sec));
        flags |= FLUX_JOB_PRE_SIGNED;
    }
#endif
    if (!(f = flux_job_submit (h, J ? J : jobspec, priority, flags)))
        log_err_exit ("flux_job_submit");
    if (flux_job_submit_get_id (f, &id) < 0) {
        log_msg_exit ("%s", future_strerror (f, errno));
    }
    printf ("%llu\n", (unsigned long long)id);
    flux_future_destroy (f);
#if HAVE_FLUX_SECURITY
    flux_security_destroy (sec); // invalidates J
#endif
    flux_close (h);
    free (jobspec);
    return 0;
}

struct attach_ctx {
    flux_t *h;
    int exit_code;
    flux_jobid_t id;
    flux_future_t *f;
    flux_watcher_t *sigint_w;
    flux_watcher_t *sigtstp_w;
    struct timespec t_sigint;
    bool show_events;
};

void attach_cancel_continuation (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0)
        log_msg ("cancel: %s", future_strerror (f, errno));
    flux_future_destroy (f);
}

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
            if (flux_job_event_watch_cancel (ctx->f) < 0)
                log_err_exit ("flux_job_event_watch_cancel");
            log_msg ("detaching...");
        }
        else {
            flux_watcher_stop (ctx->sigtstp_w);
            log_msg ("one more ctrl-Z to suspend");
        }
    }
}

void attach_event_continuation (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;
    const char *entry;
    json_t *o;
    const char *name;
    json_t *context;
    int status;

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
    if (!strcmp (name, "exception")) {
        const char *type;
        int severity;
        const char *note = NULL;

        if (json_unpack (context, "{s:s s:i s?:s}",
                         "type", &type,
                         "severity", &severity,
                         "note", &note) < 0)
            log_err_exit ("error decoding exception context");
        fprintf (stderr, "job-event: exception type=%s severity=%d %s\n",
                 type,
                 severity,
                 note ? note : "");
    }
    else {
        if (!strcmp (name, "finish")) {
            if (json_unpack (context, "{s:i}", "status", &status) < 0)
                log_err_exit ("error decoding finish context");
            if (WIFSIGNALED (status))
                ctx->exit_code = WTERMSIG (status) + 128;
            else if (WIFEXITED (status))
                ctx->exit_code = WEXITSTATUS (status);
        }
        else if (!strcmp (name, "clean")) {
            if (flux_job_event_watch_cancel (f) < 0)
                log_err_exit ("flux_job_event_watch_cancel");
        }
    }
    if (ctx->show_events && strcmp (name, "exception") != 0)
        fprintf (stderr, "job-event: %s\n", name);
    json_decref (o);
    flux_future_reset (f);
    return;
done:
    flux_future_destroy (f);
    flux_watcher_stop (ctx->sigint_w);
    flux_watcher_stop (ctx->sigtstp_w);
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
    ctx.id = parse_arg_unsigned (argv[optindex++], "jobid");
    ctx.show_events = optparse_hasopt (p, "show-events");

    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(r = flux_get_reactor (ctx.h)))
        log_err_exit ("flux_get_reactor");

    if (!(ctx.f = flux_job_event_watch (ctx.h, ctx.id)))
        log_err_exit ("flux_job_event_watch");
    if (flux_future_then (ctx.f, -1, attach_event_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");

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

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_watcher_destroy (ctx.sigint_w);
    flux_watcher_destroy (ctx.sigtstp_w);
    flux_close (ctx.h);
    return ctx.exit_code;
}

void id_convert (optparse_t *p, const char *src, char *dst, int dstsz)
{
    const char *from = optparse_get_str (p, "from", "dec");
    const char *to = optparse_get_str (p, "to", "dec");
    flux_jobid_t id;

    /* src to id
     */
    if (!strcmp (from, "dec")) {
        id = parse_arg_unsigned (src, "input");
    }
    else if (!strcmp (from, "kvs")) {
        if (strncmp (src, "job.", 4) != 0)
            log_msg_exit ("%s: missing 'job.' prefix", src);
        if (fluid_decode (src + 4, &id, FLUID_STRING_DOTHEX) < 0)
            log_msg_exit ("%s: malformed input", src);
    }
    else if (!strcmp (from, "words")) {
        if (fluid_decode (src, &id, FLUID_STRING_MNEMONIC) < 0)
            log_msg_exit ("%s: malformed input", src);
    }
    else
        log_msg_exit ("Unknown from=%s", from);

    /* id to dst
     */
    if (!strcmp (to, "dec")) {
        snprintf (dst, dstsz, "%llu", (unsigned long long)id);
    }
    else if (!strcmp (to, "kvs")) {
        if (flux_job_kvs_key (dst, dstsz, id, NULL) < 0)
            log_msg_exit ("error encoding id");
    }
    else if (!strcmp (to, "words")) {
        if (fluid_encode (dst, dstsz, id, FLUID_STRING_MNEMONIC) < 0)
            log_msg_exit ("error encoding id");
    }
    else
        log_msg_exit ("Unknown to=%s", to);
}

char *trim_string (char *s)
{
    /* trailing */
    int len = strlen (s);
    while (len > 1 && isspace (s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
    /* leading */
    char *p = s;
    while (*p && isspace (*p))
        p++;
    return p;
}

int cmd_id (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    char dst[256];

    if (optindex == argc) {
        char src[256];
        while ((fgets (src, sizeof (src), stdin))) {
            id_convert (p, trim_string (src), dst, sizeof (dst));
            printf ("%s\n", dst);
        }
    }
    else {
        while (optindex < argc) {
            id_convert (p, argv[optindex++], dst, sizeof (dst));
            printf ("%s\n", dst);
        }
    }
    return 0;
}

struct entry_format {
    const char *format;
    const char *time_format;
    double initial;
};

void entry_format_parse_options (optparse_t *p, struct entry_format *e)
{
    e->format = optparse_get_str (p, "format", "text");
    if (strcasecmp (e->format, "text")
        && strcasecmp (e->format, "json"))
        log_msg_exit ("invalid format type");
    e->time_format = optparse_get_str (p, "time-format", "raw");
    if (strcasecmp (e->time_format, "raw")
        && strcasecmp (e->time_format, "iso")
        && strcasecmp (e->time_format, "offset"))
        log_msg_exit ("invalid time-format type");
}

struct eventlog_ctx {
    optparse_t *p;
    flux_jobid_t id;
    struct entry_format e;
};

/* convert floating point timestamp (UNIX epoch, UTC) to ISO 8601 string,
 * with microsecond precision
 */
static int event_timestr (struct entry_format *e, double timestamp,
                          char *buf, size_t size)
{
    if (!strcasecmp (e->time_format, "raw")) {
        if (snprintf (buf, size, "%lf", timestamp) >= size)
            return -1;
    }
    else if (!strcasecmp (e->time_format, "iso")) {
        time_t sec = timestamp;
        unsigned long usec = (timestamp - sec)*1E6;
        struct tm tm;
        if (!gmtime_r (&sec, &tm))
            return -1;
        if (strftime (buf, size, "%FT%T", &tm) == 0)
            return -1;
        size -= strlen (buf);
        buf += strlen (buf);
        if (snprintf (buf, size, ".%.6luZ", usec) >= size)
            return -1;
    }
    else { /* !strcasecmp (e->time_format, "offset") */
        if (e->initial == 0.)
            e->initial = timestamp;
        timestamp -= e->initial;
        if (snprintf (buf, size, "%lf", timestamp) >= size)
            return -1;
    }
    return 0;
}

void output_event_text (struct entry_format *e, json_t *event)
{
    double timestamp;
    const char *name;
    json_t *context = NULL;
    char buf[128];

    if (eventlog_entry_parse (event, &timestamp, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (event_timestr (e, timestamp, buf, sizeof (buf)) < 0)
        log_msg_exit ("error converting timestamp to ISO 8601");

    printf ("%s %s", buf, name);

    if (context) {
        const char *key;
        json_t *value;
        json_object_foreach (context, key, value) {
            char *sval;
            sval = json_dumps (value, JSON_ENCODE_ANY|JSON_COMPACT);
            printf (" %s=%s", key, sval);
            free (sval);
        }
    }
    printf ("\n");
    fflush (stdout);
}

void output_event_json (json_t *event)
{
    char *e;

    if (!(e = json_dumps (event, JSON_COMPACT)))
        log_msg_exit ("json_dumps");
    printf ("%s\n", e);
    free (e);
}

void output_event (struct entry_format *e, json_t *event)
{
    if (!strcasecmp (e->format, "text"))
        output_event_text (e, event);
    else /* !strcasecmp (e->format, "json") */
        output_event_json (event);
}

void eventlog_continuation (flux_future_t *f, void *arg)
{
    struct eventlog_ctx *ctx = arg;
    const char *s;
    json_t *a = NULL;
    size_t index;
    json_t *value;

    if (flux_rpc_get_unpack (f, "{s:s}", "eventlog", &s) < 0) {
        if (errno == ENOENT) {
            flux_future_destroy (f);
            log_msg_exit ("job %lu not found", ctx->id);
        }
        else
            log_err_exit ("flux_job_eventlog_lookup_get");
    }

    if (!(a = eventlog_decode (s)))
        log_err_exit ("eventlog_decode");

    json_array_foreach (a, index, value) {
        output_event (&ctx->e, value);
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

    ctx.id = parse_arg_unsigned (argv[optindex++], "jobid");
    ctx.p = p;
    entry_format_parse_options (p, &ctx.e);

    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0,
                             "{s:I s:[s] s:i}",
                             "id", ctx.id,
                             "keys", "eventlog",
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");
    if (flux_future_then (f, -1., eventlog_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);
    return (0);
}

struct wait_event_ctx {
    optparse_t *p;
    const char *wait_event;
    double timeout;
    flux_jobid_t id;
    bool got_event;
    struct entry_format e;
    char *context_key;
    char *context_value;
};

bool wait_event_test_context (struct wait_event_ctx *ctx, json_t *context)
{
    void *iter;
    bool match = false;

    iter = json_object_iter (context);
    while (iter && !match) {
        const char *key = json_object_iter_key (iter);
        json_t *value = json_object_iter_value (iter);
        if (!strcmp (key, ctx->context_key)) {
            char *str = json_dumps (value, JSON_ENCODE_ANY|JSON_COMPACT);
            if (!strcmp (str, ctx->context_value))
                match = true;
            free (str);
        }
        /* special case, json_dumps() will put quotes around string
         * values.  Consider the case when user does not surround
         * string value with quotes */
        if (!match && json_is_string (value)) {
            const char *str = json_string_value (value);
            if (!strcmp (str, ctx->context_value))
                match = true;
        }
        iter = json_object_iter_next (context, iter);
    }
    return match;
}

bool wait_event_test (struct wait_event_ctx *ctx, json_t *event)
{
    double timestamp;
    const char *name;
    json_t *context = NULL;
    bool match = false;

    if (eventlog_entry_parse (event, &timestamp, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (ctx->e.initial == 0.)
        ctx->e.initial = timestamp;

    if (!strcmp (name, ctx->wait_event)) {
        if (ctx->context_key) {
            if (context)
                match = wait_event_test_context (ctx, context);
        }
        else
            match = true;
    }

    return match;
}

void wait_event_continuation (flux_future_t *f, void *arg)
{
    struct wait_event_ctx *ctx = arg;
    json_t *o = NULL;
    const char *event;

    if (flux_rpc_get (f, NULL) < 0) {
        if (errno == ENOENT) {
            flux_future_destroy (f);
            log_msg_exit ("job %lu not found", ctx->id);
        }
        else if (errno == ETIMEDOUT) {
            flux_future_destroy (f);
            log_msg_exit ("wait-event timeout on event '%s'\n",
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
        if (!optparse_hasopt (ctx->p, "quiet"))
            output_event (&ctx->e, o);
        if (flux_job_event_watch_cancel (f) < 0)
            log_err_exit ("flux_job_event_watch_cancel");
    } else if (optparse_hasopt (ctx->p, "verbose")) {
        if (!ctx->got_event)
            output_event (&ctx->e, o);
    }

    json_decref (o);

    flux_future_reset (f);

    /* re-call to set timeout */
    if (flux_future_then (f, ctx->timeout, wait_event_continuation, arg) < 0)
        log_err_exit ("flux_future_then");
}

int cmd_wait_event (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    struct wait_event_ctx ctx = {0};
    const char *str;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if ((argc - optindex) != 2) {
        optparse_print_usage (p);
        exit (1);
    }
    ctx.id = parse_arg_unsigned (argv[optindex++], "jobid");
    ctx.p = p;
    ctx.wait_event = argv[optindex++];
    ctx.timeout = optparse_get_duration (p, "timeout", -1.0);
    entry_format_parse_options (p, &ctx.e);
    if ((str = optparse_get_str (p, "match-context", NULL))) {
        ctx.context_key = xstrdup (str);
        ctx.context_value = strchr (ctx.context_key, '=');
        if (!ctx.context_value)
            log_msg_exit ("must specify a context test as key=value");
        *ctx.context_value++ = '\0';
    }

    if (!(f = flux_job_event_watch (h, ctx.id)))
        log_err_exit ("flux_job_event_watch");
    if (flux_future_then (f, ctx.timeout, wait_event_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    free (ctx.context_key);
    flux_close (h);
    return (0);
}

struct info_ctx {
    flux_jobid_t id;
    json_t *keys;
};

void info_output (flux_future_t *f, const char *suffix, flux_jobid_t id)
{
    const char *s;

    if (flux_rpc_get_unpack (f, "{s:s}", suffix, &s) < 0) {
        if (errno == ENOENT) {
            flux_future_destroy (f);
            log_msg_exit ("job %lu id or key not found", id);
        }
        else
            log_err_exit ("flux_rpc_get_unpack");
    }

    /* XXX - prettier output later */
    printf ("%s\n", s);
}

void info_continuation (flux_future_t *f, void *arg)
{
    struct info_ctx *ctx = arg;
    size_t index;
    json_t *key;

    json_array_foreach (ctx->keys, index, key) {
        const char *s = json_string_value (key);
        info_output (f, s, ctx->id);
    }

    flux_future_destroy (f);
}

int cmd_info (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    const char *topic = "job-info.lookup";
    struct info_ctx ctx = {0};

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if ((argc - optindex) < 2) {
        optparse_print_usage (p);
        exit (1);
    }

    ctx.id = parse_arg_unsigned (argv[optindex++], "jobid");

    if (!(ctx.keys = json_array ()))
        log_msg_exit ("json_array");

    while (optindex < argc) {
        json_t *s;
        if (!(s = json_string (argv[optindex])))
            log_msg_exit ("json_string");
        if (json_array_append_new (ctx.keys, s) < 0)
            log_msg_exit ("json_array_append");
        optindex++;
    }

    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0,
                             "{s:I s:O s:i}",
                             "id", ctx.id,
                             "keys", ctx.keys,
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");
    if (flux_future_then (f, -1., info_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    json_decref (ctx.keys);
    flux_close (h);
    return (0);
}

int cmd_drain (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    double timeout = optparse_get_duration (p, "timeout", -1.);
    flux_future_t *f;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (argc - optindex != 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(f = flux_rpc (h, "job-manager.drain", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_future_wait_for (f, timeout) < 0 || flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("drain: %s", errno == ETIMEDOUT
                                   ? "timeout" : future_strerror (f, errno));
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_undrain (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_future_t *f;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (argc - optindex != 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(f = flux_rpc (h, "job-manager.undrain", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("undrain: %s", future_strerror (f, errno));
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
