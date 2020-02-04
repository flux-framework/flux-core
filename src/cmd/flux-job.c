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
#include <czmq.h>
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
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libioencode/ioencode.h"

int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_submit (optparse_t *p, int argc, char **argv);
int cmd_attach (optparse_t *p, int argc, char **argv);
int cmd_id (optparse_t *p, int argc, char **argv);
int cmd_namespace (optparse_t *p, int argc, char **argv);
int cmd_cancel (optparse_t *p, int argc, char **argv);
int cmd_cancelall (optparse_t *p, int argc, char **argv);
int cmd_raise (optparse_t *p, int argc, char **argv);
int cmd_raiseall (optparse_t *p, int argc, char **argv);
int cmd_kill (optparse_t *p, int argc, char **argv);
int cmd_killall (optparse_t *p, int argc, char **argv);
int cmd_priority (optparse_t *p, int argc, char **argv);
int cmd_eventlog (optparse_t *p, int argc, char **argv);
int cmd_wait_event (optparse_t *p, int argc, char **argv);
int cmd_info (optparse_t *p, int argc, char **argv);
int cmd_stats (optparse_t *p, int argc, char **argv);
int cmd_wait (optparse_t *p, int argc, char **argv);

int stdin_flags;

static struct optparse_option global_opts[] =  {
    OPTPARSE_TABLE_END
};

static struct optparse_option list_opts[] =  {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Limit output to N jobs",
    },
    { .name = "states", .key = 's', .has_arg = 1, .arginfo = "STATES",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "List jobs in specific states",
    },
    { .name = "user", .key = 'u', .has_arg = 1, .arginfo = "USER",
      .usage = "Limit output to specific user. " \
               "Specify \"all\" for all users.",
    },
    { .name = "all-user", .key = 'a', .has_arg = 0,
      .usage = "List my jobs, regardless of state",
    },
    { .name = "all", .key = 'A', .has_arg = 0,
      .usage = "List jobs for all users, regardless of state",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option cancelall_opts[] =  {
    { .name = "user", .key = 'u', .has_arg = 1, .arginfo = "USER",
      .usage = "Set target user or 'all' (instance owner only)",
    },
    { .name = "states", .key = 'S', .has_arg = 1, .arginfo = "STATES",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set target job states (default=ACTIVE)",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Confirm the command",
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

static struct optparse_option raiseall_opts[] =  {
    { .name = "severity", .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set exception severity [0-7] (0 is fatal, default=7)",
    },
    { .name = "user", .key = 'u', .has_arg = 1, .arginfo = "USER",
      .usage = "Set target user or 'all' (instance owner only)",
    },
    { .name = "states", .key = 'S', .has_arg = 1, .arginfo = "STATES",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set target job states (default=ACTIVE)",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Confirm the command",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option kill_opts[] = {
    { .name = "signal", .key = 's', .has_arg = 1, .arginfo = "SIG",
      .usage = "Send signal SIG (default SIGTERM)",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option killall_opts[] = {
    { .name = "signal", .key = 's', .has_arg = 1, .arginfo = "SIG",
      .usage = "Send signal SIG (default SIGTERM)",
    },
    { .name = "user", .key = 'u', .has_arg = 1, .arginfo = "USER",
      .usage = "Set target user or 'all' (instance owner only)",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Confirm the command",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option submit_opts[] =  {
    { .name = "priority", .key = 'p', .has_arg = 1, .arginfo = "N",
      .usage = "Set job priority (0-31, default=16)",
    },
    { .name = "flags", .key = 'f', .has_arg = 3,
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set submit comma-separated flags (e.g. debug, waitable)",
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
    { .name = "show-exec", .key = 'X', .has_arg = 0,
      .usage = "Show exec events on stderr",
    },
    { .name = "label-io", .key = 'l', .has_arg = 0,
      .usage = "Label output by rank",
    },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Increase verbosity" },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Suppress warnings written to stderr from flux-job",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option id_opts[] =  {
    { .name = "from", .key = 'f', .has_arg = 1,
      .arginfo = "dec|kvs|hex|words",
      .usage = "Convert jobid from specified form",
    },
    { .name = "to", .key = 't', .has_arg = 1,
      .arginfo = "dec|kvs|hex|words",
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
    { .name = "path", .key = 'p', .has_arg = 1, .arginfo = "PATH",
      .usage = "Specify alternate eventlog path suffix "
               "(e.g. \"guest.exec.eventlog\")",
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
    { .name = "path", .key = 'p', .has_arg = 1, .arginfo = "PATH",
      .usage = "Specify alternate eventlog path suffix "
               "(e.g. \"guest.exec.eventlog\")",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "list",
      "[OPTIONS]",
      "List jobs",
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
    { "cancelall",
      "[OPTIONS] [message ...]",
      "Cancel multiple jobs",
      cmd_cancelall,
      0,
      cancelall_opts,
    },
    { "raise",
      "[OPTIONS] id [message ...]",
      "Raise exception for job",
      cmd_raise,
      0,
      raise_opts,
    },
    { "raiseall",
      "OPTIONS type [message ...]",
      "Raise an exception on multiple jobs.",
      cmd_raiseall,
      0,
      raiseall_opts,
    },
    { "kill",
      "[OPTIONS] id",
      "Send signal to running job",
      cmd_kill,
      0,
      kill_opts,
    },
    { "killall",
      "[OPTIONS]",
      "Send signal to multiple running jobs",
      cmd_killall,
      0,
      killall_opts,
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
      "[-f text|json] [-T raw|iso|offset] [-p path] id",
      "Display eventlog for a job",
      cmd_eventlog,
      0,
      eventlog_opts
    },
    { "wait-event",
      "[-f text|json] [-T raw|iso|offset] [-t seconds] [-m key=val] "
      "[-p path] id event",
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
    { "stats",
      NULL,
      "Get current job stats",
      cmd_stats,
      0,
      NULL
    },
    { "namespace",
      "[id ...]",
      "Convert job ids to job guest kvs namespace names",
      cmd_namespace,
      0,
      NULL
    },
    { "wait",
      "[id]",
      "Wait for job to complete.",
      cmd_wait,
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
        log_msg_exit ("error parsing %s: \"%s\"", name, s);
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

/* Parse an OPTPARSE_OPT_AUTOSPLIT list of state names, returning a
 * mask of states.  Exit with error if unknown state encountered.
 */
int parse_arg_states (optparse_t *p, const char *optname)
{
    int state_mask = 0;
    const char *arg;

    assert (optparse_hasopt (p, optname) == true);

    optparse_getopt_iterator_reset (p, optname);
    while ((arg = optparse_getopt_next (p, optname))) {
        flux_job_state_t state;

        if (flux_job_strtostate (arg, &state) == 0)
            state_mask |= state;
        else if (!strcasecmp (arg, "pending"))
            state_mask |= FLUX_JOB_PENDING;
        else if (!strcasecmp (arg, "running"))
            state_mask |= FLUX_JOB_RUNNING;
        else if (!strcasecmp (arg, "active"))
            state_mask |= FLUX_JOB_ACTIVE;
        else
            log_msg_exit ("error parsing --%s: %s is unknown", optname, arg);
    }
    if (state_mask == 0)
        log_msg_exit ("no states specified");
    return state_mask;
}

/* Parse user argument, which may be a username, a user id, or "all".
 * Print an error and exit if there is a problem.
 * Return numeric userid (all -> FLUX_USERID_UNKNOWN).
 */
uint32_t parse_arg_userid (optparse_t *p, const char *optname)
{
    uint32_t userid;
    const char *s = optparse_get_str (p, optname, NULL);
    struct passwd *pw;
    char *endptr;

    assert (s != NULL);
    if (!strcmp (s, "all"))
        return FLUX_USERID_UNKNOWN;
    if ((pw = getpwnam (s)))
        return pw->pw_uid;
    errno = 0;
    userid = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || !isdigit (*s))
        log_msg_exit ("unknown user %s", s);
    return userid;
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

static int raiseall (flux_t *h,
                     int dry_run,
                     uint32_t userid,
                     int state_mask,
                     int severity,
                     const char *type,
                     const char *note,
                     int *errorsp)
{
    flux_future_t *f;
    int count;
    int errors;

    if (!(f = flux_rpc_pack (h,
                             "job-manager.raiseall",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:i s:i s:i s:s s:s}",
                             "dry_run",
                             dry_run,
                             "userid",
                             userid,
                             "states",
                             state_mask,
                             "severity",
                             severity,
                             "type",
                             type,
                             "note",
                             note ? note : "")))
        log_err_exit ("error sending raiseall request");
    if (flux_rpc_get_unpack (f,
                             "{s:i s:i}",
                             "count",
                             &count,
                             "errors",
                             &errors) < 0)
        log_msg_exit ("raiseall: %s", future_strerror (f, errno));
    flux_future_destroy (f);
    if (errorsp)
        *errorsp = errors;
    return count;
}

int cmd_raiseall (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int severity = optparse_get_int (p, "severity", 7);
    const char *type;
    uint32_t userid;
    int state_mask;
    flux_t *h;
    char *note = NULL;
    int count;
    int errors;
    int dry_run = 1;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    type = argv[optindex++];
    if (optindex < argc)
        note = parse_arg_message (argv + optindex, "message");
    if (optparse_hasopt (p, "states")) {
        state_mask = parse_arg_states (p, "states");
        if ((state_mask & FLUX_JOB_INACTIVE))
            log_msg_exit ("Exceptions cannot be raised on inactive jobs");
    }
    else
        state_mask = FLUX_JOB_ACTIVE;
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = geteuid ();
    if (optparse_hasopt (p, "force"))
        dry_run = 0;
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    count = raiseall (h,
                      dry_run,
                      userid,
                      state_mask,
                      severity,
                      type,
                      note,
                      &errors);
    if (count > 0 && dry_run)
        log_msg ("Command matched %d jobs (-f to confirm)", count);
    else if (count > 0 && !dry_run)
        log_msg ("Raised exception on %d jobs (%d errors)", count, errors);
    else
        log_msg ("Command matched 0 jobs");

    flux_close (h);
    free (note);
    return 0;
}

/*
 * List generated by:
 *
 * $ kill -l | sed 's/[0-9]*)//g' | xargs -n1 printf '    "%s",\n'
 *
 * (ignoring SIGRT*)
 */
static const char *sigmap[] = {
    NULL,      /* 1 origin */
    "SIGHUP",
    "SIGINT",
    "SIGQUIT",
    "SIGILL",
    "SIGTRAP",
    "SIGABRT",
    "SIGBUS",
    "SIGFPE",
    "SIGKILL",
    "SIGUSR1",
    "SIGSEGV",
    "SIGUSR2",
    "SIGPIPE",
    "SIGALRM",
    "SIGTERM",
    "SIGSTKFLT",
    "SIGCHLD",
    "SIGCONT",
    "SIGSTOP",
    "SIGTSTP",
    "SIGTTIN",
    "SIGTTOU",
    "SIGURG",
    "SIGXCPU",
    "SIGXFSZ",
    "SIGVTALRM",
    "SIGPROF",
    "SIGWINCH",
    "SIGIO",
    "SIGPWR",
    "SIGSYS",
    NULL,
};

static bool isnumber (const char *s, int *result)
{
    char *endptr;
    long int l;

    errno = 0;
    l = strtol (s, &endptr, 10);
    if (errno
        || *endptr != '\0'
        || l <= 0) {
        return false;
    }
    *result = (int) l;
    return true;
}

static int str2signum (const char *sigstr)
{
    int i;
    if (isnumber (sigstr, &i)) {
        if (i <= 0)
            return -1;
        return i;
    }
    i = 1;
    while (sigmap[i] != NULL) {
        if (strcmp (sigstr, sigmap[i]) == 0 ||
            strcmp (sigstr, sigmap[i]+3) == 0)
            return i;
        i++;
    }
    return -1;
}

int cmd_kill (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    int optindex = optparse_option_index (p);
    flux_jobid_t id;
    const char *s;
    int signum;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    id = parse_arg_unsigned (argv[optindex++], "jobid");

    s = optparse_get_str (p, "signal", "SIGTERM");
    if ((signum = str2signum (s))< 0)
        log_msg_exit ("kill: Invalid signal %s", s);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_job_kill (h, id, signum)))
        log_err_exit ("flux_job_kill");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("kill %ju: %s",
                      (uintmax_t) id,
                      future_strerror (f, errno));
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

int cmd_killall (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    int optindex = optparse_option_index (p);
    const char *s;
    int signum;
    uint32_t userid;
    int count;
    int dry_run = 1;
    int errors;

    if (argc - optindex > 0) {
        optparse_print_usage (p);
        exit (1);
    }
    s = optparse_get_str (p, "signal", "SIGTERM");
    if ((signum = str2signum (s))< 0)
        log_msg_exit ("killall: Invalid signal %s", s);
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = geteuid ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "force"))
        dry_run = 0;
    if (!(f = flux_rpc_pack (h,
                             "job-manager.killall",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:i s:i}",
                             "dry_run",
                             dry_run,
                             "userid",
                             userid,
                             "signum",
                             signum)))
        log_err_exit ("error sending killall request");
    if (flux_rpc_get_unpack (f,
                             "{s:i s:i}",
                             "count",
                             &count,
                             "errors",
                             &errors) < 0)
        log_msg_exit ("killall: %s", future_strerror (f, errno));
    flux_future_destroy (f);
    if (count > 0 && dry_run)
        log_msg ("Command matched %d jobs (-f to confirm)", count);
    else if (count > 0 && !dry_run)
        log_msg ("%s %d jobs (%d errors)", strsignal (signum), count, errors);
    else
        log_msg ("Command matched 0 jobs");
    flux_close (h);
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

int cmd_cancelall (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    uint32_t userid;
    int state_mask;
    flux_t *h;
    char *note = NULL;
    int dry_run = 1;
    int count;
    int errors;

    if (optindex < argc)
        note = parse_arg_message (argv + optindex, "message");
    if (optparse_hasopt (p, "states")) {
        state_mask = parse_arg_states (p, "states");
        if ((state_mask & FLUX_JOB_INACTIVE))
            log_msg_exit ("Inactive jobs cannot be cancelled");
    }
    else
        state_mask = FLUX_JOB_ACTIVE;
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = geteuid ();
    if (optparse_hasopt (p, "force"))
        dry_run = 0;
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    count = raiseall (h,
                      dry_run,
                      userid,
                      state_mask,
                      0,
                      "cancel",
                      note,
                      &errors);
    if (count > 0 && dry_run)
        log_msg ("Command matched %d jobs (-f to confirm)", count);
    else if (count > 0 && !dry_run)
        log_msg ("Canceled %d jobs (%d errors)", count, errors);
    else
        log_msg ("Command matched 0 jobs");
    flux_close (h);
    free (note);
    return 0;
}

int cmd_list (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int max_entries = optparse_get_int (p, "count", 0);
    char *attrs = "[\"userid\",\"priority\",\"t_submit\",\"state\"," \
        "\"name\",\"ntasks\",\"nnodes\",\"ranks\",\"t_depend\",\"t_sched\"," \
        "\"t_run\",\"t_cleanup\",\"t_inactive\"]";
    flux_t *h;
    flux_future_t *f;
    json_t *jobs;
    size_t index;
    json_t *value;
    uint32_t userid;
    int states = 0;

    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optparse_hasopt (p, "all-user") || optparse_hasopt (p, "all"))
        states = FLUX_JOB_ACTIVE | FLUX_JOB_INACTIVE;
    else if (optparse_hasopt (p, "states"))
        states = parse_arg_states (p, "states");
    else
        states = FLUX_JOB_PENDING | FLUX_JOB_RUNNING;

    if (optparse_hasopt (p, "all"))
        userid = FLUX_USERID_UNKNOWN;
    else if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = geteuid ();

    if (!(f = flux_job_list (h, max_entries, attrs, userid, states)))
        log_err_exit ("flux_job_list");
    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0)
        log_err_exit ("flux_job_list");
    json_array_foreach (jobs, index, value) {
        char *str;
        str = json_dumps (value, 0);
        if (!str)
            log_msg_exit ("error parsing list response");
        printf ("%s\n", str);
        free (str);
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
            else if (!strcmp (name, "waitable"))
                flags |= FLUX_JOB_WAITABLE;
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
    flux_future_t *eventlog_f;
    flux_future_t *exec_eventlog_f;
    flux_future_t *output_f;
    flux_watcher_t *sigint_w;
    flux_watcher_t *sigtstp_w;
    struct timespec t_sigint;
    flux_watcher_t *stdin_w;
    zlist_t *stdin_rpcs;
    bool stdin_data_sent;
    optparse_t *p;
    bool output_header_parsed;
    int leader_rank;
    double timestamp_zero;
    int eventlog_watch_count;
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
    if (!strcmp (stream, "stdout"))
        fp = stdout;
    else
        fp = stderr;
    if (len > 0) {
        if (optparse_hasopt (ctx->p, "label-io"))
            fprintf (fp, "%s: ", rank);
        fwrite (data, len, 1, fp);
    }
    free (data);
}

static void handle_output_redirect (struct attach_ctx *ctx, json_t *context)
{
    const char *stream = NULL;
    const char *rank = NULL;
    const char *path = NULL;
    if (!ctx->output_header_parsed)
        log_msg_exit ("stream redirect read before header");
    if (json_unpack (context, "{ s:s s:s s?:s }",
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
                        "{ s?i s:i s:s s?:s s?:s s?:i }",
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
    }
}

/* Handle an event in the guest.output eventlog.
 * This is a stream of responses, one response per event, terminated with
 * an ENODATA error response (or another error if something went wrong).
 * The first eventlog entry is a header; remaining entries are data,
 * redirect, or log messages.  Print each data entry to stdout/stderr,
 * with task/rank prefix if --label-io was specified.  For each redirect entry, print
 * information on paths to redirected locations if --quiet is not
 * speciifed.
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

    if (!strcmp (name, "header")) {
        /* Future: per-stream encoding */
        ctx->output_header_parsed = true;
    }
    else if (!strcmp (name, "data")) {
        handle_output_data (ctx, context);
    }
    else if (!strcmp (name, "redirect")) {
        handle_output_redirect (ctx, context);
    }
    else if (!strcmp (name, "log")) {
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
    (void)fcntl (STDIN_FILENO, F_SETFL, stdin_flags);
}

static void attach_send_shell_completion (flux_future_t *f, void *arg)
{
    struct attach_ctx *ctx = arg;

    /* failng to write stdin to service is (generally speaking) a
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
                              const void *buf,
                              int len,
                              bool eof)
{
    json_t *context = NULL;
    char topic[1024];
    flux_future_t *f = NULL;
    int saved_errno;
    int rc = -1;

    snprintf (topic, sizeof (topic), "shell-%ju.stdin", (uintmax_t)ctx->id);
    if (!(context = ioencode ("stdin", "all", buf, len, eof)))
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
    flux_buffer_t *fb;
    const char *ptr;
    int len;

    fb = flux_buffer_read_watcher_get_buffer (w);
    assert (fb);

    if (!(ptr = flux_buffer_read_line (fb, &len)))
        log_err_exit ("flux_buffer_read_line on stdin");

    if (len > 0) {
        if (attach_send_shell (ctx, ptr, len, false) < 0)
            log_err_exit ("attach_send_shell");
        ctx->stdin_data_sent = true;
    }
    else {
        /* possibly left over data before EOF */
        if (!(ptr = flux_buffer_read (fb, -1, &len)))
            log_err_exit ("flux_buffer_read on stdin");

        if (len > 0) {
            if (attach_send_shell (ctx, ptr, len, false) < 0)
                log_err_exit ("attach_send_shell");
            ctx->stdin_data_sent = true;
        }
        else {
            if (attach_send_shell (ctx, NULL, 0, true) < 0)
                log_err_exit ("attach_send_shell");
            flux_watcher_stop (ctx->stdin_w);
        }
    }
}

/*  Start the guest.output eventlog watcher
 */
void attach_output_start (struct attach_ctx *ctx)
{
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

    if (!strcmp (name, "shell.init")) {
        flux_watcher_t *w;

        if (json_unpack (context, "{s:i}",
                         "leader-rank", &ctx->leader_rank) < 0)
            log_err_exit ("error decoding shell.init context");

        /* flux_buffer_read_watcher_create() requires O_NONBLOCK on
         * stdin */

        if ((stdin_flags = fcntl (STDIN_FILENO, F_GETFL)) < 0)
            log_err_exit ("fcntl F_GETFL stdin");
        if (atexit (restore_stdin_flags) != 0)
            log_err_exit ("atexit");
        if (fcntl (STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK) < 0)
            log_err_exit ("fcntl F_SETFL stdin");

        w = flux_buffer_read_watcher_create (flux_get_reactor (ctx->h),
                                             STDIN_FILENO,
                                             1 << 20,
                                             attach_stdin_cb,
                                             FLUX_WATCHER_LINE_BUFFER,
                                             ctx);
        if (!w)
            log_err_exit ("flux_buffer_read_watcher_create");

        if (!(ctx->stdin_rpcs = zlist_new ()))
            log_err_exit ("zlist_new");

        ctx->stdin_w = w;
        flux_watcher_start (ctx->stdin_w);

        attach_output_start (ctx);
    }

    /*  If job is complete, and we haven't started watching
     *   output eventlog, then start now in case shell.init event
     *   was never emitted (failure in iniitialization)
     */
    if (!strcmp (name, "complete") && !ctx->output_f)
        attach_output_start (ctx);

    if (optparse_hasopt (ctx->p, "show-exec")) {
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
    json_t *o;
    double timestamp;
    const char *name;
    json_t *context;
    int status;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        if (errno == ENOENT)
            log_msg_exit ("Failed to attach to %ju: No such job", ctx->id);
        log_msg_exit ("flux_job_event_watch_get: %s",
                      future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        log_err_exit ("eventlog_entry_decode");
    if (eventlog_entry_parse (o, &timestamp, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");

    if (ctx->timestamp_zero == 0.)
        ctx->timestamp_zero = timestamp;

    if (!strcmp (name, "exception")) {
        const char *type;
        int severity;
        const char *note = NULL;

        if (json_unpack (context, "{s:s s:i s?:s}",
                         "type", &type,
                         "severity", &severity,
                         "note", &note) < 0)
            log_err_exit ("error decoding exception context");
        fprintf (stderr, "%.3fs: job.exception type=%s severity=%d %s\n",
                         timestamp - ctx->timestamp_zero,
                         type,
                         severity,
                         note ? note : "");
    }
    else if (!strcmp (name, "submit")) {
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
        if (!strcmp (name, "finish")) {
            if (json_unpack (context, "{s:i}", "status", &status) < 0)
                log_err_exit ("error decoding finish context");
            if (WIFSIGNALED (status)) {
                ctx->exit_code = WTERMSIG (status) + 128;
                log_msg ("task(s) %s", strsignal (WTERMSIG (status)));
            }
            else if (WIFEXITED (status)) {
                ctx->exit_code = WEXITSTATUS (status);
                if (ctx->exit_code != 0)
                    log_msg ("task(s) exited with exit code %d",
                             ctx->exit_code);
            }
        }
    }

    if (optparse_hasopt (ctx->p, "show-events")
                                    && strcmp (name, "exception") != 0) {
        print_eventlog_entry (stderr,
                              "job",
                              timestamp - ctx->timestamp_zero,
                              name,
                              context);
    }

    json_decref (o);
    flux_future_reset (f);
    return;
done:
    flux_future_destroy (f);
    ctx->eventlog_f = NULL;
    ctx->eventlog_watch_count--;
    attach_completed_check (ctx);
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
    ctx.p = p;

    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(r = flux_get_reactor (ctx.h)))
        log_err_exit ("flux_get_reactor");

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

    /*  Ignore SIGTTIN, SIGTTOU.
     *
     *  SIGTTIN is ignored to avoid flux-job attach being stopped while
     *   in the background. Normally, background flux-job attach doesn't
     *   register activity on stdin, so this is not necessary. However,
     *   in some cases (e.g. docker run -ti), activity on the terminal
     *   does seem to wakeup epoll on background processes, and ignoring
     *   SIGTTIN is a workaround in those cases.
     *   (https://github.com/flux-framework/flux-core/issues/2599)
     *
     *  SIGTTOU is ignored so that flux-job attach can still write to
     *   stderr/out even when in the background on a terminal with the
     *   TOSTOP output mode set (also rare, but possible)
     */
    signal (SIGTTIN, SIG_IGN);
    signal (SIGTTOU, SIG_IGN);

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

    zlist_destroy (&(ctx.stdin_rpcs));
    flux_watcher_destroy (ctx.sigint_w);
    flux_watcher_destroy (ctx.sigtstp_w);
    flux_watcher_destroy (ctx.stdin_w);
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
    else if (!strcmp (from, "hex")) {
        if (fluid_decode (src, &id, FLUID_STRING_DOTHEX) < 0)
            log_msg_exit ("%s: malformed input", src);
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
    else if (!strcmp (to, "hex")) {
        if (fluid_encode (dst, dstsz, id, FLUID_STRING_DOTHEX) < 0)
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

static void print_job_namespace (const char *src)
{
    char ns[64];
    flux_jobid_t id = parse_arg_unsigned (src, "jobid");
    if (flux_job_kvs_namespace (ns, sizeof (ns), id) < 0)
        log_msg_exit ("error getting kvs namespace for %ju", id);
    printf ("%s\n", ns);
}

int cmd_namespace (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);

    if (optindex == argc) {
        char src[256];
        while ((fgets (src, sizeof (src), stdin)))
            print_job_namespace (trim_string (src));
    }
    else {
        while (optindex < argc)
            print_job_namespace (argv[optindex++]);
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
    const char *path;
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

    if (flux_rpc_get_unpack (f, "{s:s}", ctx->path, &s) < 0) {
        if (errno == ENOENT) {
            flux_future_destroy (f);
            if (!strcmp (ctx->path, "eventlog"))
                log_msg_exit ("job %lu not found", ctx->id);
            else
                log_msg_exit ("eventlog path %s not found", ctx->path);
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
    ctx.path = optparse_get_str (p, "path", "eventlog");
    ctx.p = p;
    entry_format_parse_options (p, &ctx.e);

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
    return (0);
}

struct wait_event_ctx {
    optparse_t *p;
    const char *wait_event;
    double timeout;
    flux_jobid_t id;
    const char *path;
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
            if (!strcmp (ctx->path, "eventlog"))
                log_msg_exit ("job %lu not found", ctx->id);
            else
                log_msg_exit ("eventlog path %s not found", ctx->path);
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
    ctx.path = optparse_get_str (p, "path", "eventlog");
    entry_format_parse_options (p, &ctx.e);
    if ((str = optparse_get_str (p, "match-context", NULL))) {
        ctx.context_key = xstrdup (str);
        ctx.context_value = strchr (ctx.context_key, '=');
        if (!ctx.context_value)
            log_msg_exit ("must specify a context test as key=value");
        *ctx.context_value++ = '\0';
    }

    if (!(f = flux_job_event_watch (h, ctx.id, ctx.path, 0)))
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

int cmd_stats (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    const char *topic = "job-info.job-stats";
    const char *s;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc (h, topic, NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_rpc_get (f, &s) < 0)
        log_msg_exit ("stats: %s", future_strerror (f, errno));

    /* for time being, just output json object for result */
    printf ("%s\n", s);
    flux_close (h);
    return (0);
}

int cmd_wait (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    flux_jobid_t id = FLUX_JOBID_ANY;
    bool success;
    const char *errstr;

    if ((argc - optindex) > 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (optindex < argc)
        id = parse_arg_unsigned (argv[optindex++], "jobid");
    if (!(f = flux_job_wait (h, id)))
        log_err_exit ("flux_job_wait");
    if (flux_job_wait_get_status (f, &success, &errstr) < 0)
        log_msg_exit ("%s", flux_future_error_string (f));
    if (id == FLUX_JOBID_ANY) {
        if (flux_job_wait_get_id (f, &id) < 0)
            log_err_exit ("flux_job_wait_get_id");
        printf ("%ju\n", (uintmax_t)id);
    }
    if (!success)
        log_msg_exit ("%s", errstr);
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
