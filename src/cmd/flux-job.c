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
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <pwd.h>
#include <assert.h>
#include <locale.h>
#include <jansson.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <sys/ioctl.h>
#include <signal.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <flux/hostlist.h>
#include <flux/taskmap.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libjob/job.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/strstrip.h"
#include "src/common/libutil/sigutil.h"
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libioencode/ioencode.h"
#include "src/shell/mpir/proctable.h"
#include "src/common/libdebugged/debugged.h"
#include "src/common/libterminus/pty.h"
#include "src/common/libtaskmap/taskmap_private.h"
#include "src/common/librlist/rlist.h"
#include "ccan/str/str.h"

#ifndef VOLATILE
# if defined(__STDC__) || defined(__cplusplus)
# define VOLATILE volatile
# else
# define VOLATILE
# endif
#endif

#define MPIR_NULL                  0
#define MPIR_DEBUG_SPAWNED         1
#define MPIR_DEBUG_ABORTING        2

VOLATILE int MPIR_debug_state    = MPIR_NULL;
struct proctable *proctable      = NULL;
MPIR_PROCDESC *MPIR_proctable    = NULL;
int MPIR_proctable_size          = 0;
char *MPIR_debug_abort_string    = NULL;
int MPIR_i_am_starter            = 1;
int MPIR_acquired_pre_main       = 1;
int MPIR_force_to_main           = 1;
int MPIR_partial_attach_ok       = 1;
char *totalview_jobid            = NULL;

int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_list_inactive (optparse_t *p, int argc, char **argv);
int cmd_status (optparse_t *p, int argc, char **argv);
int cmd_list_ids (optparse_t *p, int argc, char **argv);
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
int cmd_urgency (optparse_t *p, int argc, char **argv);
int cmd_eventlog (optparse_t *p, int argc, char **argv);
int cmd_wait_event (optparse_t *p, int argc, char **argv);
int cmd_info (optparse_t *p, int argc, char **argv);
int cmd_stats (optparse_t *p, int argc, char **argv);
int cmd_wait (optparse_t *p, int argc, char **argv);
int cmd_memo (optparse_t *p, int argc, char **argv);
int cmd_purge (optparse_t *p, int argc, char **argv);
int cmd_taskmap (optparse_t *p, int argc, char **argv);
int cmd_timeleft (optparse_t *p, int argc, char **argv);
int cmd_last (optparse_t *p, int argc, char **argv);

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

static struct optparse_option list_inactive_opts[] =  {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Limit output to N jobs",
    },
    { .name = "since", .key = 's', .has_arg = 1, .arginfo = "T",
      .usage = "Limit output to jobs that entered the inactive state since"
               " timestamp T",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option list_ids_opts[] =  {
    { .name = "wait-state", .key = 'W', .has_arg = 1, .arginfo = "STATE",
      .usage = "Return only after jobid has reached specified state",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option urgency_opts[] =  {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Output old urgency value on success",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option cancel_opts[] =  {
    { .name = "message", .key = 'm', .has_arg = 1, .arginfo = "NOTE",
      .usage = "Set cancel exception note",
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
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Suppress output if no jobs match",
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
    { .name = "message", .key = 'm', .has_arg = 1, .arginfo = "NOTE",
      .usage = "Set exception note",
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
    { .name = "urgency", .key = 'u', .has_arg = 1, .arginfo = "N",
      .usage = "Set job urgency (0-31), hold=0, default=16, expedite=31",
    },
    { .name = "flags", .key = 'f', .has_arg = 1,
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

static struct optparse_option status_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Increase verbosity"
    },
    { .name = "exception-exit-code", .key = 'e', .has_arg = 1,
      .group = 1,
      .arginfo = "N",
      .usage = "Set the default exit code for any jobs that terminate"
               " solely due to an exception (e.g. canceled jobs or"
               " jobs rejected by the scheduler) to N [default=1]"
    },
    { .name = "json", .key = 'j', .has_arg = 0,
      .usage = "Dump job result information gleaned from eventlog to stdout",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option id_opts[] =  {
    { .name = "to", .key = 't', .has_arg = 1,
      .arginfo = "dec|kvs|hex|dothex|words|f58",
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
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "COUNT",
      .usage = "required number of matches (default 1)",
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
    { .name = "waitcreate", .key = 'W', .has_arg = 0,
      .usage = "If path does not exist, wait for its creation",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option info_opts[] =  {
    { .name = "original", .key = 'o', .has_arg = 0,
      .usage = "For key \"jobspec\", return the original submitted jobspec",
    },
    { .name = "base", .key = 'b', .has_arg = 0,
      .usage = "For key \"jobspec\" or \"R\", "
               "do not apply updates from eventlog",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option wait_opts[] =  {
    { .name = "all", .key = 'a', .has_arg = 0,
      .usage = "Wait for all (waitable) jobs",
    },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Emit a line of output for all jobs, not just failing ones",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option memo_opts[] = {
    { .name = "volatile", .has_arg = 0,
      .usage = "Memo will not appear in eventlog (will be lost on restart)",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option purge_opts[] = {
    { .name = "age-limit", .has_arg = 1, .arginfo = "FSD",
      .usage = "Purge jobs that became inactive beyond age-limit.",
    },
    { .name = "num-limit", .has_arg = 1, .arginfo = "COUNT",
      .usage = "Purge oldest inactive jobs until COUNT are left.",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Perform the irreversible purge.",
    },
    { .name = "batch", .has_arg = 1, .arginfo = "COUNT",
      .usage = "Limit number of jobs per request (default 50).",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option taskmap_opts[] = {
    { .name = "taskids", .has_arg = 1, .arginfo = "NODEID",
      .usage = "Print idset of tasks on node NODEID",
    },
    { .name = "ntasks", .has_arg = 1, .arginfo = "NODEID",
      .usage = "Print number of tasks on node NODEID",
    },
    { .name = "nodeid", .has_arg = 1, .arginfo = "TASKID",
      .usage = "Print the shell rank/nodeid on which a taskid executed",
    },
    { .name = "hostname", .has_arg = 1, .arginfo = "TASKID",
      .usage = "Print the hostname on which a taskid executed",
    },
    { .name = "to", .has_arg = 1, .arginfo="FORMAT",
      .usage = "Convert an RFC 34 taskmap to another format "
               "(FORMAT can be raw, pmi, or multiline)",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option timeleft_opts[] = {
    { .name = "human", .key = 'H', .has_arg = 0,
      .usage = "Output in Flux Standard Duration instead of seconds.",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "list",
      "[OPTIONS]",
      "List jobs",
      cmd_list,
      OPTPARSE_SUBCMD_HIDDEN,
      list_opts
    },
    { "list-inactive",
      "[OPTIONS]",
      "List Inactive jobs",
      cmd_list_inactive,
      OPTPARSE_SUBCMD_HIDDEN,
      list_inactive_opts
    },
    { "list-ids",
      "[OPTIONS] ID [ID ...]",
      "List job(s) by id",
      cmd_list_ids,
      OPTPARSE_SUBCMD_HIDDEN,
      list_ids_opts,
    },
    { "urgency",
      "[OPTIONS] id urgency",
      "Set job urgency (0-31, HOLD, EXPEDITE, DEFAULT)",
      cmd_urgency,
      0,
      urgency_opts,
    },
    { "cancel",
      "[OPTIONS] ids... [--] [message ...]",
      "Cancel one or more jobs",
      cmd_cancel,
      0,
      cancel_opts,
    },
    { "cancelall",
      "[OPTIONS] [message ...]",
      "Cancel multiple jobs",
      cmd_cancelall,
      0,
      cancelall_opts,
    },
    { "raise",
      "[OPTIONS] ids... [--] [message ...]",
      "Raise exception on one or more jobs",
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
      "[OPTIONS] ids...",
      "Send signal to one or more running jobs",
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
    { "status",
      "id [id...]",
      "Wait for job(s) to complete and exit with largest exit code",
      cmd_status,
      0,
      status_opts,
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
      "[-f text|json] [-T raw|iso|offset] [-t seconds] [-m key=val] [-c <num>] "
      "[-p path] [-W] [-q] [-v] id event",
      "Wait for an event ",
      cmd_wait_event,
      0,
      wait_event_opts
    },
    { "info",
      "id key",
      "Display info for a job",
      cmd_info,
      0,
      info_opts
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
      "[--all] [id]",
      "Wait for job(s) to complete.",
      cmd_wait,
      0,
      wait_opts,
    },
    { "memo",
      "[--volatile] id key=value [key=value, ...]",
      "Post an RFC 21 memo to a job",
      cmd_memo,
      0,
      memo_opts,
    },
    { "taskmap",
      "[OPTION] JOBID|TASKMAP",
      "Utility function for working with job task maps",
      cmd_taskmap,
      0,
      taskmap_opts,
    },
    { "timeleft",
      "[JOBID]",
      "Find remaining runtime for job or enclosing instance",
      cmd_timeleft,
      0,
      timeleft_opts,
    },
    { "last",
      "SLICE",
      "List my most recently submitted job id(s)",
      cmd_last,
      0,
      NULL,
    },
    { "purge",
      "[--age-limit=FSD] [--num-limit=N] [--batch=COUNT] [--force] [ID ...]",
      "Purge the oldest inactive jobs",
      cmd_purge,
      0,
      purge_opts,
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
        if (!(s->flags & OPTPARSE_SUBCMD_HIDDEN))
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

    /*  Initialize locale from environment. Allows unicode character
     *   prefix in F58 encoded JOBIDs in wide-character capable locales.
     */
    setlocale (LC_ALL, "");

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

static flux_jobid_t parse_jobid (const char *s)
{
    flux_jobid_t id;
    if (flux_job_id_parse (s, &id) < 0)
        log_msg_exit ("error parsing jobid: \"%s\"", s);
    return id;
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
            state_mask |= FLUX_JOB_STATE_PENDING;
        else if (!strcasecmp (arg, "running"))
            state_mask |= FLUX_JOB_STATE_RUNNING;
        else if (!strcasecmp (arg, "active"))
            state_mask |= FLUX_JOB_STATE_ACTIVE;
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
    if (streq (s, "all"))
        return FLUX_USERID_UNKNOWN;
    if ((pw = getpwnam (s)))
        return pw->pw_uid;
    errno = 0;
    userid = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || !isdigit (*s))
        log_msg_exit ("unknown user %s", s);
    return userid;
}

int cmd_urgency (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_future_t *f;
    int urgency, old_urgency;
    flux_jobid_t id;
    const char *jobid = NULL;
    const char *urgencystr = NULL;

    if (optindex != argc - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    jobid = argv[optindex++];
    id = parse_jobid (jobid);
    urgencystr = argv[optindex++];
    if (!strcasecmp (urgencystr, "hold"))
        urgency = FLUX_JOB_URGENCY_HOLD;
    else if (!strcasecmp (urgencystr, "expedite"))
        urgency = FLUX_JOB_URGENCY_EXPEDITE;
    else if (!strcasecmp (urgencystr, "default"))
        urgency = FLUX_JOB_URGENCY_DEFAULT;
    else
        urgency = parse_arg_unsigned (urgencystr, "urgency");

    if (!(f = flux_job_set_urgency (h, id, urgency)))
        log_err_exit ("flux_job_set_urgency");
    if (flux_rpc_get_unpack (f, "{s:i}", "old_urgency", &old_urgency) < 0)
        log_msg_exit ("%s: %s", jobid, future_strerror (f, errno));
    if (optparse_hasopt (p, "verbose")) {
        if (old_urgency == FLUX_JOB_URGENCY_HOLD)
            fprintf (stderr, "old urgency: job held\n");
        else if (old_urgency == FLUX_JOB_URGENCY_EXPEDITE)
            fprintf (stderr, "old urgency: job expedited\n");
        else
            fprintf (stderr, "old urgency: %d\n", old_urgency);
    }
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}


struct jobid_arg {
    const char *arg;
    flux_jobid_t id;
};

static void jobid_arg_free (void **item)
{
    if (item) {
        struct jobid_arg *arg = *item;
        free (arg);
        arg = NULL;
    }
}

static struct jobid_arg *jobid_arg_create (const char *s)
{
    flux_jobid_t id;
    struct jobid_arg *arg;

    if (flux_job_id_parse (s, &id) < 0
        || !(arg = calloc (1, sizeof (*arg))))
        return NULL;
    arg->id = id;
    arg->arg = s;

    return arg;
}

/*  Parse a command line containing a list of jobids and optional
 *   message (or note). Stops processing at first argument that
 *   is not a jobid and, if note != NULL, consumes the rest of the
 *   args and returns as a single string in `note`.
 */
static int parse_jobids_and_note (optparse_t *p,
                                  char **argv,
                                  zlistx_t **args,
                                  char **note)
{
    if (!(*args = zlistx_new ()))
        return -1;
    zlistx_set_destructor (*args, jobid_arg_free);

    /*  Convert each argument to a jobid, stopping at the first failure
     */
    while (*argv) {
        struct jobid_arg *arg;
        if (!(arg = jobid_arg_create (*argv))) {
            if (errno == EINVAL)
                break;
            log_err_exit ("Error processing argument %s", *argv);
        }
        if (!zlistx_add_end (*args, arg))
            log_msg_exit ("Unable to add jobid %s to list", *argv);
        argv++;
    }

    if (*argv != NULL) {
        /*  If a note was not expected, then this command takes
         *  only jobids, and a non-jobid argument should raise a
         *  fatal error.
         */
        if (note == NULL)
            log_msg_exit ("invalid jobid: %s", *argv);

        /*  Forward past `--`, which may have been used to force
         *  separation of jobid and message on command line
         */
        if (streq (*argv, "--"))
            argv++;
        *note = parse_arg_message (argv, "message");
    }
    if (note) {
        /*  If a note was expected, also see if --message was used to set
         *  it on the cmdline. It is an error to specify both --message and
         *  the note in free arguments.
         */
        const char *s;
        if (optparse_getopt (p, "message", &s) && *note)
            log_msg_exit ("Do not set note on command line and with --message");
        else if (s && !(*note = strdup (s)))
            log_err_exit ("failed to duplicate --message=%s", s);
    }
    return 0;
}

/*  Check the wait-all future in `f` and print any errors, one per line.
 */
static int wait_all_check (flux_future_t *f, const char *prefix)
{
    int rc;
    if ((rc = flux_future_get (f, NULL)) < 0) {
        const char *child = flux_future_first_child (f);
        while (child) {
            flux_future_t *cf = flux_future_get_child (f, child);
            if (flux_future_get (cf, NULL) < 0)
                log_msg ("%s %s: %s",
                         prefix,
                         child,
                         future_strerror (cf, errno));
            child = flux_future_next_child (f);
        }
    }
    return rc;
}

int cmd_raise (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int severity = optparse_get_int (p, "severity", 0);
    const char *type = optparse_get_str (p, "type", "cancel");
    flux_t *h;
    char *note = NULL;
    flux_future_t *f;
    zlistx_t *args;
    struct jobid_arg *arg;
    int rc = 0;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    parse_jobids_and_note (p, argv + optindex, &args, &note);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_future_wait_all_create ()))
        log_err_exit ("flux_future_wait_all_create");

    /* Always need to set handle for wait_all future: */
    flux_future_set_flux (f, h);

    arg = zlistx_first (args);
    while (arg) {
        flux_future_t *rf = flux_job_raise (h, arg->id, type, severity, note);
        if (!rf || flux_future_push (f, arg->arg, rf) < 0)
            log_err_exit ("flux_job_raise");
        arg = zlistx_next (args);
    }
    rc = wait_all_check (f, "raise");

    zlistx_destroy (&args);
    flux_future_destroy (f);
    flux_close (h);
    free (note);
    return rc;
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
        if ((state_mask & FLUX_JOB_STATE_INACTIVE))
            log_msg_exit ("Exceptions cannot be raised on inactive jobs");
    }
    else
        state_mask = FLUX_JOB_STATE_ACTIVE;
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = getuid ();
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

int cmd_kill (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    int optindex = optparse_option_index (p);
    zlistx_t *args;
    struct jobid_arg *arg;
    const char *s;
    int signum;
    int rc = 0;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    parse_jobids_and_note (p, argv + optindex, &args, NULL);

    s = optparse_get_str (p, "signal", "SIGTERM");
    if ((signum = sigutil_signum (s)) < 0)
        log_msg_exit ("kill: Invalid signal %s", s);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_future_wait_all_create ()))
        log_err_exit ("flux_future_wait_all_create");
    flux_future_set_flux (f, h);

    arg = zlistx_first (args);
    while (arg) {
        flux_future_t *rf = flux_job_kill (h, arg->id, signum);
        if (!rf || flux_future_push (f, arg->arg, rf) < 0)
            log_err_exit ("flux_job_kill");
        arg = zlistx_next (args);
    }
    rc = wait_all_check (f, "kill");

    flux_future_destroy (f);
    flux_close (h);
    return rc;
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
    if ((signum = sigutil_signum (s)) < 0)
        log_msg_exit ("killall: Invalid signal %s", s);
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = getuid ();

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
    char *note = NULL;
    zlistx_t *args;
    struct jobid_arg *arg;
    flux_future_t *f;
    int rc = 0;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    parse_jobids_and_note (p, argv + optindex, &args, &note);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_future_wait_all_create ()))
        log_err_exit ("flux_future_wait_all_create");
    flux_future_set_flux (f, h);

    arg = zlistx_first (args);
    while (arg) {
        flux_future_t *rf = flux_job_cancel (h, arg->id, note);
        if (!rf || flux_future_push (f, arg->arg, rf) < 0)
            log_err_exit ("flux_job_cancel");
        arg = zlistx_next (args);
    }
    rc = wait_all_check (f, "cancel");

    zlistx_destroy (&args);
    flux_future_destroy (f);
    flux_close (h);
    free (note);
    return rc;
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
        if ((state_mask & FLUX_JOB_STATE_INACTIVE))
            log_msg_exit ("Inactive jobs cannot be canceled");
    }
    else
        state_mask = FLUX_JOB_STATE_ACTIVE;
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = getuid ();
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
    else if (!optparse_hasopt (p, "quiet"))
        log_msg ("Command matched 0 jobs");
    flux_close (h);
    free (note);
    return 0;
}

int cmd_list (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int max_entries = optparse_get_int (p, "count", 0);
    flux_t *h;
    flux_future_t *f;
    json_t *jobs;
    size_t index;
    json_t *value;
    uint32_t userid;
    int states = 0;
    json_t *c;

    if (isatty (STDOUT_FILENO)) {
        fprintf (stderr,
                 "This is not the command you are looking for. "
                 "Try flux-jobs(1).\n");
        exit (1);
    }
    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optparse_hasopt (p, "all-user") || optparse_hasopt (p, "all"))
        states = FLUX_JOB_STATE_ACTIVE | FLUX_JOB_STATE_INACTIVE;
    else if (optparse_hasopt (p, "states"))
        states = parse_arg_states (p, "states");
    else
        states = FLUX_JOB_STATE_PENDING | FLUX_JOB_STATE_RUNNING;

    if (optparse_hasopt (p, "all"))
        userid = FLUX_USERID_UNKNOWN;
    else if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = getuid ();

    if (!(c = json_pack ("{ s:[ {s:[i]}, {s:[i]} ] }",
                         "and",
                         "userid", userid,
                         "states", states)))
        log_msg_exit ("failed to construct constraint object");

    if (!(f = flux_rpc_pack (h,
                             "job-list.list",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i s:[s] s:o}",
                             "max_entries", max_entries,
                             "attrs", "all",
                             "constraint", c)))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0)
        log_err_exit ("flux job-list.list");
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

int cmd_list_inactive (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int max_entries = optparse_get_int (p, "count", 0);
    double since = optparse_get_double (p, "since", 0.);
    flux_t *h;
    flux_future_t *f;
    json_t *jobs;
    size_t index;
    json_t *value;
    json_t *c;

    if (isatty (STDOUT_FILENO)) {
        fprintf (stderr,
                 "This is not the command you are looking for. "
                 "Try flux-jobs(1).\n");
        exit (1);
    }
    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(c = json_pack ("{s:[i]}", "states", FLUX_JOB_STATE_INACTIVE)))
        log_msg_exit ("failed to construct constraint object");

    if (!(f = flux_rpc_pack (h,
                             "job-list.list",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i s:f s:[s] s:o}",
                             "max_entries", max_entries,
                             "since", since,
                             "attrs", "all",
                             "constraint", c)))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0)
        log_err_exit ("flux job-list.list");
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

void list_id_continuation (flux_future_t *f, void *arg)
{
    json_t *job;
    char *str;
    if (flux_rpc_get_unpack (f, "{s:o}", "job", &job) < 0)
        log_err_exit ("flux_job_list_id");
    str = json_dumps (job, 0);
    if (!str)
        log_msg_exit ("error parsing list-id response");
    printf ("%s\n", str);
    free (str);
    flux_future_destroy (f);
}

int cmd_list_ids (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    int i, ids_len;
    flux_job_state_t state;
    const char *state_str;

    if (isatty (STDOUT_FILENO)) {
        fprintf (stderr,
                 "This is not the command you are looking for. "
                 "Try flux-jobs(1).\n");
        exit (1);
    }
    if ((argc - optindex) < 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* if no job state specified by user, pick first job state of
     * depend, which means will return as soon as the job-list module
     * is aware of the job
     */
    state_str = optparse_get_str (p, "wait-state", "depend");
    if (flux_job_strtostate (state_str, &state) < 0)
        log_msg_exit ("invalid job state specified");

    ids_len = argc - optindex;
    for (i = 0; i < ids_len; i++) {
        flux_jobid_t id = parse_jobid (argv[optindex + i]);
        flux_future_t *f;
        if (!(f = flux_rpc_pack (h,
                                 "job-list.list-id",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[s] s:i}",
                                 "id", id,
                                 "attrs",
                                   "all",
                                 "state", state)))
            log_err_exit ("flux_rpc_pack");
        if (flux_future_then (f, -1, list_id_continuation, NULL) < 0)
            log_err_exit ("flux_future_then");
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

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

    if (streq (name, "-"))
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

static void print_jobid (flux_jobid_t id)
{
    printf ("%s\n", idf58 (id));
}

int cmd_submit (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
#if HAVE_FLUX_SECURITY
    flux_security_t *sec = NULL;
    const char *sec_config;
    const char *sign_type = NULL;
#endif
    int flags = 0;
    void *jobspec;
    int jobspecsz;
    const char *J = NULL;
    int urgency;
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
            if (streq (name, "debug"))
                flags |= FLUX_JOB_DEBUG;
            else if (streq (name, "waitable"))
                flags |= FLUX_JOB_WAITABLE;
            else if (streq (name, "signed"))
                flags |= FLUX_JOB_PRE_SIGNED;
            else if (streq (name, "novalidate"))
                flags |= FLUX_JOB_NOVALIDATE;
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
        if (flags & FLUX_JOB_PRE_SIGNED)
            log_msg ("Ignoring security config with --flags=pre-signed");
        else {
            sec_config = optparse_get_str (p, "security-config", NULL);
            if (!(sec = flux_security_create (0)))
                log_err_exit ("security");
            if (flux_security_configure (sec, sec_config) < 0)
                log_err_exit ("security config %s",
                              flux_security_last_error (sec));
            sign_type = optparse_get_str (p, "sign-type", NULL);
        }
    }
#endif
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if ((jobspecsz = read_jobspec (input, &jobspec)) == 0)
        log_msg_exit ("required jobspec is empty");

    /* If jobspec was pre-signed, then assign J to to jobspec after
     * stripping surrounding whitespace.
     */
    if (flags & FLUX_JOB_PRE_SIGNED)
        J = strstrip (jobspec);

    urgency = optparse_get_int (p, "urgency", FLUX_JOB_URGENCY_DEFAULT);

#if HAVE_FLUX_SECURITY
    if (sec) {
        if (!(J = flux_sign_wrap (sec, jobspec, jobspecsz, sign_type, 0)))
            log_err_exit ("flux_sign_wrap: %s",
                          flux_security_last_error (sec));
        flags |= FLUX_JOB_PRE_SIGNED;
    }
#endif
    if (!(f = flux_job_submit (h, J ? J : jobspec, urgency, flags)))
        log_err_exit ("flux_job_submit");
    if (flux_job_submit_get_id (f, &id) < 0) {
        log_msg_exit ("%s", future_strerror (f, errno));
    }
    print_jobid (id);
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

    if (!(ptr = flux_buffer_read_watcher_get_data (w, &len)))
        log_err_exit ("flux_buffer_read_line on stdin");
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

static void setup_mpir_proctable (const char *s)
{
    if (!(proctable = proctable_from_json_string (s))) {
        errno = EINVAL;
        log_err_exit ("proctable_from_json_string");
    }
    MPIR_proctable = proctable_get_mpir_proctable (proctable,
                                                   &MPIR_proctable_size);
    if (!MPIR_proctable) {
        errno = EINVAL;
        log_err_exit ("proctable_get_mpir_proctable");
    }
}

static void gen_attach_signal (struct attach_ctx *ctx)
{
    flux_future_t *f = NULL;
    if (!(f = flux_job_kill (ctx->h, ctx->id, SIGCONT)))
        log_err_exit ("flux_job_kill");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("kill %s: %s",
                      ctx->jobid,
                      future_strerror (f, errno));
    flux_future_destroy (f);
}

static void setup_mpir_interface (struct attach_ctx *ctx, json_t *context)
{
    char topic [1024];
    const char *s = NULL;
    flux_future_t *f = NULL;
    int stop_tasks_in_exec = 0;

    if (json_unpack (context, "{s?b}", "sync", &stop_tasks_in_exec) < 0)
        log_err_exit ("error decoding shell.init context");

    snprintf (topic, sizeof (topic), "%s.proctable", ctx->service);

    if (!(f = flux_rpc_pack (ctx->h, topic, ctx->leader_rank, 0, "{}")))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get (f, &s) < 0)
        log_err_exit ("%s", topic);

    setup_mpir_proctable (s);
    flux_future_destroy (f);

    MPIR_debug_state = MPIR_DEBUG_SPAWNED;

    /* Signal the parallel debugger */
    MPIR_Breakpoint ();

    if (stop_tasks_in_exec || optparse_hasopt (ctx->p, "debug-emulate")) {
        /* To support MPIR_partial_attach_ok, we need to send SIGCONT to
         * those MPI processes to which the debugger didn't attach.
         * However, all of the debuggers that I know of do ignore
         * additional SIGCONT being sent to the processes they attached to.
         * Therefore, we send SIGCONT to *every* MPI process.
         *
         * We also send SIGCONT under the debug-emulate flag. This allows us
         * to write a test for attach mode. The running job will exit
         * on SIGCONT.
         */
        gen_attach_signal (ctx);
    }
}

static void attach_setup_stdin (struct attach_ctx *ctx)
{
    flux_watcher_t *w;
    int flags = 0;

    if (ctx->readonly)
        return;

    if (!ctx->unbuffered)
        flags = FLUX_WATCHER_LINE_BUFFER;

    /* flux_buffer_read_watcher_create() requires O_NONBLOCK on
     * stdin */

    if ((stdin_flags = fd_set_nonblocking (STDIN_FILENO)) < 0)
        log_err_exit ("unable to set stdin nonblocking");
    if (atexit (restore_stdin_flags) != 0)
        log_err_exit ("atexit");

    w = flux_buffer_read_watcher_create (flux_get_reactor (ctx->h),
                                         STDIN_FILENO,
                                         1 << 20,
                                         attach_stdin_cb,
                                         flags,
                                         ctx);
    if (!w) {
        /* Users have reported rare occurrences of an EINVAL error
         * from flux_buffer_read_watcher_create(), the cause of which
         * is not understood (See issue #5175). In many cases, perhaps all,
         * stdin is not used by the job, so aborting `flux job attach`
         * is an unnecessary failure. Therefore, just ignore stdin when
         * errno is EINVAL here:
         */
        if (errno == EINVAL) {
            log_msg ("Warning: ignoring stdin: failed to create watcher");
            return;
        }
        log_err_exit ("flux_buffer_read_watcher_create");
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
        if (MPIR_being_debugged)
            setup_mpir_interface (ctx, context);
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
    const char *msg;
    if (!event_name)
        return;
    if (ctx->statusline
        && !ctx->fatal_exception
        && (msg = job_event_notify_string (event_name))) {
        int dt = ts - ctx->timestamp_zero;
        int width = 80;
        struct winsize w;

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
    free (ctx.stdin_ranks);

    if (ctx.fatal_exception && ctx.exit_code == 0)
        ctx.exit_code = 1;

    return ctx.exit_code;
}

static void result_cb (flux_future_t *f, void *arg)
{}

/*  Translate status to an exit code as would be done by UNIX shell:
 */
static int status_to_exitcode (int status)
{
    if (status < 0)
        return 0;
    if (WIFSIGNALED (status))
        return 128 + WTERMSIG (status);
    return WEXITSTATUS (status);
}

int cmd_status (optparse_t *p, int argc, char **argv)
{
    flux_t *h = NULL;
    flux_future_t **futures = NULL;
    int exit_code;
    int i;
    int njobs;
    int verbose = optparse_getopt (p, "verbose", NULL);
    int json = optparse_getopt (p, "json", NULL);
    int optindex = optparse_option_index (p);
    int exception_exit_code = optparse_get_int (p, "exception-exit-code", 1);

    if ((njobs = (argc - optindex)) < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(futures = calloc (njobs, sizeof (*futures))))
        log_err_exit ("Failed to initialize futures array");

    for (i = 0; i < njobs; i++) {
        flux_jobid_t id = parse_jobid (argv[optindex+i]);
        if (!(futures[i] = flux_job_result (h, id, 0)))
            log_err_exit ("flux_job_wait_status");
        if (flux_future_then (futures[i], -1., result_cb, NULL) < 0)
            log_err_exit ("flux_future_then");
    }

    if (verbose && njobs > 1)
        log_msg ("fetching status for %d jobs", njobs);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err ("flux_reactor_run");

    if (verbose && njobs > 1)
        log_msg ("all done.");

    exit_code = 0;
    for (i = 0; i < njobs; i++) {
        const char *jobid = argv[optindex+i];
        int status = -1;
        int exitcode;
        int exception = 0;
        int exc_severity = 0;
        const char *exc_type = NULL;

        if (json) {
            const char *s = NULL;
            if (flux_job_result_get (futures[i], &s) < 0)
                log_err_exit ("flux_job_result_get");
            printf ("%s\n", s);
        }

        if (flux_job_result_get_unpack (futures[i],
                                        "{s:b s?s s?i s?i}",
                                        "exception_occurred", &exception,
                                        "exception_type", &exc_type,
                                        "exception_severity", &exc_severity,
                                        "waitstatus", &status) < 0) {
            if (errno == ENOENT)
                log_msg_exit ("%s: No such job", jobid);
            log_err_exit ("%s: flux_job_wait_status_get", jobid);
        }
        if ((exitcode = status_to_exitcode (status)) > exit_code)
            exit_code = exitcode;
        if (exception && exc_severity == 0 && exception_exit_code > exit_code)
            exit_code = exception_exit_code;
        if (optparse_hasopt (p, "verbose")) {
            if (WIFSIGNALED (status)) {
                log_msg ("%s: job shell died by signal %d",
                         jobid,
                         WTERMSIG (status));
            }
            else if (verbose > 1 || exitcode != 0) {
                if (exception && exc_severity == 0)
                    log_msg ("%s: exception type=%s",
                             jobid,
                             exc_type);
                else
                    log_msg ("%s: exited with exit code %d",
                             jobid,
                             exitcode);
            }
        }
        flux_future_destroy (futures[i]);
    }
    flux_close (h);
    free (futures);
    return exit_code;
}

void id_convert (optparse_t *p, const char *src, char *dst, int dstsz)
{
    const char *to = optparse_get_str (p, "to", "dec");
    flux_jobid_t id;

    /*  Parse as any valid JOBID
     */
    if (flux_job_id_parse (src, &id) < 0)
        log_msg_exit ("%s: malformed input", src);

    /*  Now encode into requested representation:
     */
    if (flux_job_id_encode (id, to, dst, dstsz) < 0) {
        if (errno == EPROTO)
            log_msg_exit ("Unknown to=%s", to);
        log_msg_exit ("Unable to encode id %s to %s", src, to);
    }
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

    /*  Require at least one id to be processed for success.
     *  Thus, `flux job id` with no input or args will exit with
     *   non-zero exit code.
     */
    int rc = -1;

    if (optindex == argc) {
        char src[256];
        while ((fgets (src, sizeof (src), stdin))) {
            id_convert (p, trim_string (src), dst, sizeof (dst));
            printf ("%s\n", dst);
            rc = 0;
        }
    }
    else {
        while (optindex < argc) {
            id_convert (p, argv[optindex++], dst, sizeof (dst));
            printf ("%s\n", dst);
            rc = 0;
        }
    }
    return rc;
}

static void print_job_namespace (const char *src)
{
    char ns[64];
    flux_jobid_t id = parse_jobid (src);
    if (flux_job_kvs_namespace (ns, sizeof (ns), id) < 0)
        log_msg_exit ("error getting kvs namespace for %s", src);
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
    const char *jobid;
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

    ctx.jobid = argv[optindex++];
    ctx.id = parse_jobid (ctx.jobid);
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
    const char *jobid;
    flux_jobid_t id;
    const char *path;
    bool got_event;
    struct entry_format e;
    char *context_key;
    char *context_value;
    int count;
    int match_count;
};

bool wait_event_test_context (struct wait_event_ctx *ctx, json_t *context)
{
    void *iter;
    bool match = false;

    iter = json_object_iter (context);
    while (iter && !match) {
        const char *key = json_object_iter_key (iter);
        json_t *value = json_object_iter_value (iter);
        if (streq (key, ctx->context_key)) {
            char *str = json_dumps (value, JSON_ENCODE_ANY|JSON_COMPACT);
            if (streq (str, ctx->context_value))
                match = true;
            free (str);
        }
        /* special case, json_dumps() will put quotes around string
         * values.  Consider the case when user does not surround
         * string value with quotes */
        if (!match && json_is_string (value)) {
            const char *str = json_string_value (value);
            if (streq (str, ctx->context_value))
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

void wait_event_continuation (flux_future_t *f, void *arg)
{
    struct wait_event_ctx *ctx = arg;
    json_t *o = NULL;
    const char *event;

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
}

int cmd_wait_event (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    struct wait_event_ctx ctx = {0};
    const char *str;
    double timeout;
    int flags = 0;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if ((argc - optindex) != 2) {
        optparse_print_usage (p);
        exit (1);
    }
    ctx.jobid = argv[optindex++];
    ctx.id = parse_jobid (ctx.jobid);
    ctx.p = p;
    ctx.wait_event = argv[optindex++];
    ctx.path = optparse_get_str (p, "path", "eventlog");
    timeout = optparse_get_duration (p, "timeout", -1.0);
    if (optparse_hasopt (p, "waitcreate"))
        flags |= FLUX_JOB_EVENT_WATCH_WAITCREATE;
    entry_format_parse_options (p, &ctx.e);
    if ((str = optparse_get_str (p, "match-context", NULL))) {
        ctx.context_key = xstrdup (str);
        ctx.context_value = strchr (ctx.context_key, '=');
        if (!ctx.context_value)
            log_msg_exit ("must specify a context test as key=value");
        *ctx.context_value++ = '\0';
    }
    ctx.count = optparse_get_int (p, "count", 1);
    if (ctx.count <= 0)
        log_msg_exit ("count must be > 0");

    if (!(f = flux_job_event_watch (h, ctx.id, ctx.path, flags)))
        log_err_exit ("flux_job_event_watch");
    if (flux_future_then (f, timeout, wait_event_continuation, &ctx) < 0)
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    free (ctx.context_key);
    flux_close (h);
    return (0);
}

char *reconstruct_current_jobspec (const char *jobspec_str,
                                   const char *eventlog_str)
{
    json_t *jobspec;
    json_t *eventlog;
    size_t index;
    json_t *entry;
    char *result;

    if (!(jobspec = json_loads (jobspec_str, 0, NULL)))
        log_msg_exit ("error decoding jobspec");
    if (!(eventlog = eventlog_decode (eventlog_str)))
        log_msg_exit ("error decoding eventlog");
    json_array_foreach (eventlog, index, entry) {
        const char *name;
        json_t *context;

        if (eventlog_entry_parse (entry, NULL, &name, &context) < 0)
            log_msg_exit ("error decoding eventlog entry");
        if (streq (name, "jobspec-update")) {
            const char *path;
            json_t *value;

            json_object_foreach (context, path, value) {
                if (jpath_set (jobspec, path, value) < 0)
                    log_err_exit ("failed to update jobspec");
            }
        }
    }
    if (!(result = json_dumps (jobspec, JSON_COMPACT)))
        log_msg_exit ("failed to encode jobspec object");
    json_decref (jobspec);
    json_decref (eventlog);
    return result;
}

void info_usage (void)
{
    fprintf (stderr,
             "Usage: flux job info id key\n"
             "some useful keys are:\n"
             "  J                    - signed jobspec\n"
             "  R                    - allocated resources\n"
             "  eventlog             - primary job eventlog\n"
             "  jobspec              - job specification\n"
             "  guest.exec.eventlog  - execution eventlog\n"
             "  guest.input          - job input log\n"
             "  guest.output         - job output log\n"
             "Use flux job info -h to list available options\n");

}

int cmd_info (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_jobid_t id;
    const char *id_str;
    const char *key;
    flux_future_t *f;
    const char *val;
    char *new_val = NULL;

    // Usage: flux job info id key
    if (argc - optindex != 2) {
        info_usage ();
        exit (1);
    }
    id_str = argv[optindex++];
    id = parse_jobid (id_str);
    key = argv[optindex++];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* The --original (pre-frobnication) jobspec is obtained by fetching J.
     * J is the original jobspec, signed, so we must unwrap it to get to the
     * delicious jobspec inside.
     */
    if (optparse_hasopt (p, "original") && streq (key, "jobspec")) {
        flux_error_t error;
        if (!(f = flux_rpc_pack (h,
                                 "job-info.lookup",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[s] s:i}",
                                 "id", id,
                                 "keys", "J",
                                 "flags", 0))
            || flux_rpc_get_unpack (f, "{s:s}", "J", &val) < 0)
            log_msg_exit ("%s", future_strerror (f, errno ));
        if (!(new_val = flux_unwrap_string (val, false, NULL, &error)))
            log_msg_exit ("Failed to unwrap J to get jobspec: %s", error.text);
        val = new_val;
    }
    /* The current (non --base) jobspec has to be reconstructed by fetching
     * the jobspec and the eventlog and updating the former with the latter.
     */
    else if (!optparse_hasopt (p, "base") && streq (key, "jobspec")) {
        const char *jobspec;
        const char *eventlog;

        // fetch the two keys in parallel
        if (!(f = flux_rpc_pack (h,
                                 "job-info.lookup",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[ss] s:i}",
                                 "id", id,
                                 "keys", "jobspec", "eventlog",
                                 "flags", 0))
            || flux_rpc_get_unpack (f,
                                    "{s:s s:s}",
                                    "jobspec", &jobspec,
                                    "eventlog", &eventlog) < 0)
            log_msg_exit ("%s", future_strerror (f, errno ));
        val = new_val = reconstruct_current_jobspec (jobspec, eventlog);
    }
    /* The current (non --base) R is obtained through the
     * job-info.update-lookup RPC, not the normal job-info.lookup.
     */
    else if (!optparse_hasopt (p, "base") && streq (key, "R")) {
        json_t *o;
        if (!(f = flux_rpc_pack (h,
                                 "job-info.update-lookup",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:s s:i}",
                                 "id", id,
                                 "key", key,
                                 "flags", 0))
            || flux_rpc_get_unpack (f, "{s:o}", key, &o) < 0)
            log_msg_exit ("%s", future_strerror (f, errno ));
        if (!(new_val = json_dumps (o, JSON_COMPACT)))
            log_msg_exit ("error encoding R object");
        val = new_val;
    }
    /* All other keys are obtained this way.
     */
    else {
        if (!(f = flux_rpc_pack (h,
                                 "job-info.lookup",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[s] s:i}",
                                 "id", id,
                                 "keys", key,
                                 "flags", 0))
            || flux_rpc_get_unpack (f, "{s:s}", key, &val) < 0)
            log_msg_exit ("%s", future_strerror (f, errno ));
    }

    printf ("%s\n", val);

    free (new_val);
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_stats (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    const char *topic = "job-list.job-stats";
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
    int rc = 0;

    if ((argc - optindex) > 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < argc) {
        id = parse_jobid (argv[optindex++]);
        if (optparse_hasopt (p, "all"))
            log_err_exit ("jobid not supported with --all");
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "all")) {
        for (;;) {
            if (!(f = flux_job_wait (h, FLUX_JOBID_ANY)))
                log_err_exit ("flux_job_wait");
            if (flux_job_wait_get_status (f, &success, &errstr) < 0) {
                if (errno == ECHILD) { // no more waitable jobs
                    flux_future_destroy (f);
                    break;
                }
                log_msg_exit ("flux_job_wait_get_status: %s",
                              future_strerror (f, errno));
            }
            if (flux_job_wait_get_id (f, &id) < 0)
                log_msg_exit ("flux_job_wait_get_id: %s",
                              future_strerror (f, errno));
            if (!success) {
                fprintf (stderr, "%s: %s\n", idf58 (id), errstr);
                rc = 1;
            }
            else {
                if (optparse_hasopt (p, "verbose"))
                    fprintf (stderr,
                             "%s: job completed successfully\n",
                             idf58 (id));
            }
            flux_future_destroy (f);
        }
    }
    else {
        if (!(f = flux_job_wait (h, id)))
            log_err_exit ("flux_job_wait");
        if (flux_job_wait_get_status (f, &success, &errstr) < 0) {
            /* ECHILD == no more waitable jobs or not waitable,
             * exit code 2 instead of 1 */
            if (errno == ECHILD)
                rc = 2;
            else
                rc = 1;
            log_msg ("%s", flux_future_error_string (f));
            goto out;
        }
        if (id == FLUX_JOBID_ANY) {
            if (flux_job_wait_get_id (f, &id) < 0)
                log_err_exit ("flux_job_wait_get_id");
            printf ("%s\n", idf58 (id));
        }
        if (!success)
            log_msg_exit ("%s", errstr);
        flux_future_destroy (f);
    }
out:
    flux_close (h);
    return (rc);
}

int cmd_memo (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_jobid_t id;
    json_t *memo = NULL;
    flux_future_t *f;
    int i;

    if ((argc - optindex) < 2) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    id = parse_jobid (argv[optindex]);

    /*  Build memo object from multiple values */
    for (i = optindex + 1; i < argc; i++) {
        void *valbuf = NULL;
        char *key;
        char *value;
        json_t *val;

        if (!(key = strdup (argv[i])))
            log_msg_exit ("memo: out of memory duplicating key");
        value = strchr (key, '=');
        if (!value)
            log_msg_exit ("memo: no value for key=%s", key);
        *value++ = '\0';

        if (streq (value, "-")) {
            ssize_t size;
            if ((size = read_all (STDIN_FILENO, &valbuf)) < 0)
                log_err_exit ("read_all");
            value = valbuf;
        }

        /* if value is not legal json, assume string */
        if (!(val = json_loads (value, JSON_DECODE_ANY, NULL))) {
            if (!(val = json_string (value)))
                log_msg_exit ("json_string");
        }

        if (!(memo = jpath_set_new (memo, key, val)))
            log_err_exit ("failed to set %s=%s in memo object\n", key, value);

        free (valbuf);
        free (key);
    }

    if (!(f = flux_rpc_pack (h,
                             "job-manager.memo",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:b s:o}",
                             "id", id,
                             "volatile", optparse_hasopt (p, "volatile"),
                             "memo", memo)))
        log_err_exit ("flux_rpc_pack");

    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("memo: %s", future_strerror (f, errno));

    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static void purge_finish (flux_t *h, int force, int total)
{
    int inactives;
    flux_future_t *f;

    if (!(f = flux_rpc (h, "job-manager.stats-get", NULL, 0, 0))
        || flux_rpc_get_unpack (f, "{s:i}", "inactive_jobs", &inactives) < 0)
        log_err_exit ("purge: failed to fetch inactive job count");
    flux_future_destroy (f);

    if (force)
        printf ("purged %d inactive jobs, %d remaining\n", total, inactives);
    else {
        printf ("use --force to purge %d of %d inactive jobs\n",
                total,
                inactives);
    }
}

static int purge_range (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    double age_limit = optparse_get_duration (p, "age-limit", -1.);
    int num_limit = optparse_get_int (p, "num-limit", -1);
    int batch = optparse_get_int (p, "batch", 50);
    int force = 0;
    int count;
    int total = 0;

    if (optparse_hasopt (p, "force"))
        force = 1;
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    do {
        flux_future_t *f;
        if (!(f = flux_rpc_pack (h,
                                 "job-manager.purge",
                                 0,
                                 0,
                                 "{s:f s:i s:i s:b}",
                                 "age_limit", age_limit,
                                 "num_limit", num_limit,
                                 "batch", batch,
                                 "force", force))
            || flux_rpc_get_unpack (f, "{s:i}", "count", &count) < 0)
            log_msg_exit ("purge: %s", future_strerror (f, errno));
        total += count;
        flux_future_destroy (f);
    } while (force && count == batch);

    purge_finish (h, force, total);
    flux_close (h);
    return 0;
}

static void purge_id_continuation (flux_future_t *f, void *arg)
{
    int *count = arg;
    int tmp;
    if (flux_rpc_get_unpack (f, "{s:i}", "count", &tmp) < 0)
        log_msg_exit ("purge: %s", future_strerror (f, errno));
    (*count) += tmp;
    flux_future_destroy (f);
}

static int purge_ids (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    int force = 0;
    int total = 0;
    int i, ids_len;

    if (optparse_hasopt (p, "force"))
        force = 1;
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    ids_len = argc - optindex;
    for (i = 0; i < ids_len; i++) {
        flux_jobid_t id = parse_jobid (argv[optindex + i]);
        flux_future_t *f;
        if (!(f = flux_rpc_pack (h,
                                 "job-manager.purge-id",
                                 0,
                                 0,
                                 "{s:I s:i}",
                                 "id", id,
                                 "force", force)))
            log_err_exit ("job-manager.purge-id");
        if (flux_future_then (f, -1, purge_id_continuation, &total) < 0)
            log_err_exit ("flux_future_then");
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    purge_finish (h, force, total);
    flux_close (h);
    return 0;
}

int cmd_purge (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    if ((argc - optindex) > 0)
        return purge_ids (p, argc, argv);
    return purge_range (p, argc, argv);
}

static struct taskmap *flux_job_taskmap (flux_jobid_t id)
{
    struct taskmap *map;
    flux_t *h;
    flux_future_t *f;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_job_event_watch (h, id, "guest.exec.eventlog", 0)))
        log_err_exit ("flux_job_event_watch");
    while (true) {
        json_t *o;
        json_t *context;
        const char *event;
        const char *name;
        if (flux_job_event_watch_get (f, &event) < 0) {
            if (errno == ENODATA)
                log_msg_exit ("No taskmap found for job");
            if (errno == ENOENT)
                log_msg_exit ("Unable to get job taskmap: no such job");
            log_msg_exit ("waiting for shell.start event: %s",
                          future_strerror (f, errno));
        }
        if (!(o = eventlog_entry_decode (event)))
            log_err_exit ("eventlog_entry_decode");
        if (eventlog_entry_parse (o, NULL, &name, &context) < 0)
            log_err_exit ("eventlog_entry_parse");
        if (streq (name, "shell.start")) {
            flux_error_t error;
            json_t *omap;
            if (!(omap = json_object_get (context, "taskmap"))
                || !(map = taskmap_decode_json (omap, &error)))
                log_msg_exit ("failed to get taskmap from shell.start event");
            json_decref (o);
            flux_job_event_watch_cancel (f);
            break;
        }
        json_decref (o);
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    flux_close (h);
    return map;
}

static char *flux_job_nodeid_to_hostname (flux_jobid_t id, int nodeid)
{
    flux_t *h;
    flux_future_t *f;
    char *result;
    const char *host;
    const char *R;
    struct rlist *rl;
    struct hostlist *hl;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc_pack (h, "job-info.lookup", FLUX_NODEID_ANY, 0,
                             "{s:I s:[s] s:i}",
                             "id", id,
                             "keys", "R",
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get_unpack (f, "{s:s}", "R", &R) < 0
        || !(rl = rlist_from_R (R))
        || !(hl = rlist_nodelist (rl)))
        log_err_exit ("failed to get hostlist for job");
    if (!(host = hostlist_nth (hl, nodeid)))
        log_err_exit ("failed to get hostname for node %d", nodeid);
    result = strdup (host);
    hostlist_destroy (hl);
    rlist_destroy (rl);
    flux_future_destroy (f);
    return result;
}

int cmd_taskmap (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    struct taskmap *map = NULL;
    int val;
    const char *to;
    char *s;
    flux_jobid_t id;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (flux_job_id_parse (argv[optindex], &id) < 0) {
        flux_error_t error;
        if (!(map = taskmap_decode (argv[optindex], &error)))
            log_msg_exit ("error decoding taskmap: %s", error.text);
    }
    else
        map = flux_job_taskmap (id);

    if ((val = optparse_get_int (p, "taskids", -1)) != -1) {
        const struct idset *ids = taskmap_taskids (map, val);
        if (!ids || !(s = idset_encode (ids, IDSET_FLAG_RANGE)))
            log_err_exit ("No taskids for node %d", val);
        printf ("%s\n", s);
        free (s);
        taskmap_destroy (map);
        return 0;
    }
    if ((val = optparse_get_int (p, "ntasks", -1)) != -1) {
        int result = taskmap_ntasks (map, val);
        if (result < 0)
            log_err_exit ("failed to get task count for node %d", val);
        printf ("%d\n", result);
        taskmap_destroy (map);
        return 0;
    }
    if ((val = optparse_get_int (p, "nodeid", -1)) != -1
        || (val = optparse_get_int (p, "hostname", -1)) != -1) {
        int result = taskmap_nodeid (map, val);
        if (result < 0)
            log_err_exit ("failed to get nodeid for task %d", val);
        if (optparse_hasopt (p, "hostname")) {
            char *host = flux_job_nodeid_to_hostname (id, result);
            printf ("%s\n", host);
            free (host);
        }
        else
            printf ("%d\n", result);
        taskmap_destroy (map);
        return 0;
    }
    if ((to = optparse_get_str (p, "to", NULL))) {
        if (streq (to, "raw"))
            s = taskmap_encode (map, TASKMAP_ENCODE_RAW);
        else if (streq (to, "pmi"))
            s = taskmap_encode (map, TASKMAP_ENCODE_PMI);
        else if (streq (to, "multiline")) {
            for (int i = 0; i < taskmap_total_ntasks (map); i++) {
                printf ("%d: %d\n", i, taskmap_nodeid (map, i));
            }
            taskmap_destroy (map);
            return 0;
        }
        else
            log_msg_exit ("invalid value --to=%s", to);
        if (s == NULL)
            log_err_exit ("failed to convert taskmap to %s", to);
        printf ("%s\n", s);
        free (s);
        taskmap_destroy (map);
        return 0;
    }
    if (!(s = taskmap_encode (map, 0)))
        log_err_exit ("taskmap_encode");
    printf ("%s\n", s);
    free (s);
    taskmap_destroy (map);
    return 0;
}

int cmd_timeleft (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_error_t error;
    double t;

    if (optindex < argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optindex < argc) {
        if (setenv ("FLUX_JOB_ID", argv[optindex++], 1) < 0)
            log_err_exit ("setenv");
    }
    if (flux_job_timeleft (h, &error, &t) < 0)
        log_msg_exit ("%s", error.text);
    if (optparse_hasopt (p, "human")) {
        char buf[64];
        if (fsd_format_duration (buf, sizeof (buf), t) < 0)
            log_err_exit ("fsd_format_duration");
        printf ("%s\n", buf);
    }
    else {
        unsigned long int sec;
        /*  Report whole seconds remaining in job, unless value is
         *  infinity, in which case we report UINT_MAX, or if value
         *  is 0 < t < 1, in which case round up to 1 to avoid
         *  printing "0" which would mean the job has expired.
         */
        if (isinf (t))
            sec = UINT_MAX;
        else if ((sec = floor (t)) == 0 && t > 0.)
            sec = 1;
        printf ("%lu\n", sec);
    }
    flux_close (h);
    return 0;
}

int cmd_last (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    flux_t *h;
    json_t *jobs;
    size_t index;
    json_t *entry;
    const char *slice = "[:1]";
    char sbuf[16];

    if (optindex < argc) {
        slice = argv[optindex++];
        // if slice doesn't contain '[', assume 'flux job last N' form
        if (!strchr (slice, '[')) {
            errno = 0;
            char *endptr;
            int n = strtol (slice, &endptr, 10);
            if (errno != 0 || *endptr != '\0') {
                optparse_print_usage (p);
                exit (1);
            }
            snprintf (sbuf, sizeof (sbuf), "[:%d]", n);
            slice = sbuf;
        }
    }
    if (optindex < argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_pack (h,
                             "job-manager.history.get",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "slice", slice))
        || flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0) {
        log_msg_exit ("%s", future_strerror (f, errno));
    }
    if (json_array_size (jobs) == 0)
        log_msg_exit ("job history is empty");
    json_array_foreach (jobs, index, entry)
        printf ("%s\n", idf58 (json_integer_value (entry)));
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
