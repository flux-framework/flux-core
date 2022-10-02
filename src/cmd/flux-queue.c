/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-queue.c - control job manager queue
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <jansson.h>
#include <argz.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"

int cmd_enable (optparse_t *p, int argc, char **argv);
int cmd_disable (optparse_t *p, int argc, char **argv);
int cmd_start (optparse_t *p, int argc, char **argv);
int cmd_stop (optparse_t *p, int argc, char **argv);
int cmd_status (optparse_t *p, int argc, char **argv);
int cmd_drain (optparse_t *p, int argc, char **argv);
int cmd_idle (optparse_t *p, int argc, char **argv);

int stdin_flags;

static struct optparse_option global_opts[] =  {
    OPTPARSE_TABLE_END
};

static struct optparse_option stop_opts[] = {
    { .name = "verbose", .key = 'v',
      .usage = "Display more detail about internal job manager state",
    },
    { .name = "quiet",
      .usage = "Display only errors",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option start_opts[] = {
    { .name = "verbose", .key = 'v',
      .usage = "Display more detail about internal job manager state",
    },
    { .name = "quiet",
      .usage = "Display only errors",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option status_opts[] = {
    { .name = "verbose", .key = 'v',
      .usage = "Display more detail about internal job manager state",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option drain_opts[] =  {
    { .name = "timeout", .key = 't', .has_arg = 1, .arginfo = "DURATION",
      .usage = "timeout after DURATION",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option idle_opts[] =  {
    { .name = "timeout", .key = 't', .has_arg = 1, .arginfo = "DURATION",
      .usage = "timeout after DURATION",
    },
    { .name = "quiet", .has_arg = 0,
      .usage = "Only display pending job count if nonzero",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "enable",
      "[OPTIONS]",
      "Enable job submission",
      cmd_enable,
      0,
      NULL,
    },
    { "disable",
      "[OPTIONS] [message ...]",
      "Disable job submission",
      cmd_disable,
      0,
      NULL,
    },
    { "start",
      "[OPTIONS]",
      "Start scheduling",
      cmd_start,
      0,
      start_opts,
    },
    { "stop",
      "[OPTIONS] [message ...]",
      "Stop scheduling",
      cmd_stop,
      0,
      stop_opts,
    },
    { "status",
      "[OPTIONS]",
      "Get queue status",
      cmd_status,
      0,
      status_opts,
    },
    { "drain",
      "[OPTIONS]",
      "Wait for queue to become empty.",
      cmd_drain,
      0,
      drain_opts
    },
    { "idle",
      "[OPTIONS]",
      "Wait for queue to become idle.",
      cmd_idle,
      0,
      idle_opts
    },
    OPTPARSE_SUBCMD_END
};

int usage (optparse_t *p, struct optparse_option *o, const char *optarg)
{
    struct optparse_subcommand *s;
    optparse_print_usage (p);
    fprintf (stderr, "\n");
    fprintf (stderr, "Common commands from flux-queue:\n");
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

    log_init ("flux-queue");

    p = optparse_create ("flux-queue");

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

void submit_admin (flux_t *h,
                   int query_only,
                   int enable,
                   const char *reason)
{
    flux_future_t *f;
    if (!(f = flux_rpc_pack (h,
                             "job-manager.submit-admin",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:b s:s}",
                             "query_only",
                             query_only,
                             "enable",
                             enable,
                             "reason",
                             reason ? reason : "")))
        log_err_exit ("error sending submit-admin request");
    if (flux_rpc_get_unpack (f,
                             "{s:b s:s}",
                             "enable",
                             &enable,
                             "reason",
                             &reason) < 0)
        log_msg_exit ("submit-admin: %s", future_strerror (f, errno));
    printf ("Job submission is %s%s%s\n",
            enable ? "enabled" : "disabled",
            enable ? "" : ": ",
            enable ? "" : reason);
    flux_future_destroy (f);
}

void alloc_admin (flux_t *h,
                  bool verbose,
                  bool quiet,
                  int query_only,
                  int enable,
                  const char *reason)
{
    flux_future_t *f;
    int free_pending;
    int alloc_pending;
    int queue_length;
    int running;

    if (!(f = flux_rpc_pack (h,
                             "job-manager.alloc-admin",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:b s:s}",
                             "query_only",
                             query_only,
                             "enable",
                             enable,
                             "reason",
                             reason ? reason : "")))
        log_err_exit ("error sending alloc-admin request");
    if (flux_rpc_get_unpack (f,
                             "{s:b s:s s:i s:i s:i s:i}",
                             "enable",
                             &enable,
                             "reason",
                             &reason,
                             "queue_length",
                             &queue_length,
                             "alloc_pending",
                             &alloc_pending,
                             "free_pending",
                             &free_pending,
                             "running",
                             &running) < 0)
        log_msg_exit ("alloc-admin: %s", future_strerror (f, errno));
    if (!quiet) {
        printf ("Scheduling is %s%s%s\n",
                enable ? "enabled" : "disabled",
                reason && strlen (reason) > 0 ? ": " : "",
                reason ? reason : "");
    }
    if (verbose) {
        printf ("%d alloc requests queued\n", queue_length);
        printf ("%d alloc requests pending to scheduler\n", alloc_pending);
        printf ("%d free requests pending to scheduler\n", free_pending);
        printf ("%d running jobs\n", running);
    }
    flux_future_destroy (f);
}

int cmd_enable (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);

    if (argc - optindex > 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    submit_admin (h, 0, 1, NULL);
    flux_close (h);
    return (0);
}

int cmd_disable (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    char *reason = NULL;

    if (argc - optindex > 0)
        reason = parse_arg_message (argv + optindex, "reason");
    else
        log_msg_exit ("submit: reason is required for disable");
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    submit_admin (h, 0, 0, reason);
    flux_close (h);
    free (reason);
    return (0);
}

int cmd_start (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);

    if (argc - optindex > 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    alloc_admin (h,
                 optparse_hasopt (p, "verbose"),
                 optparse_hasopt (p, "quiet"),
                 0,
                 1,
                 NULL);
    flux_close (h);
    return (0);
}

int cmd_stop (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    char *reason = NULL;

    if (argc - optindex > 0)
        reason = parse_arg_message (argv + optindex, "reason");
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    alloc_admin (h,
                 optparse_hasopt (p, "verbose"),
                 optparse_hasopt (p, "quiet"),
                 0,
                 0,
                 reason);
    flux_close (h);
    free (reason);
    return (0);
}

int cmd_status (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);

    if (argc - optindex > 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    submit_admin (h, 1, 0, NULL);
    alloc_admin (h,
                 optparse_hasopt (p, "verbose"),
                 false,
                 1,
                 0,
                 NULL);
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

int cmd_idle (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    double timeout = optparse_get_duration (p, "timeout", -1.);
    flux_future_t *f;
    int pending;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (argc - optindex != 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(f = flux_rpc (h, "job-manager.idle", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_future_wait_for (f, timeout) < 0
            || flux_rpc_get_unpack (f, "{s:i}", "pending", &pending) < 0)
        log_msg_exit ("idle: %s", errno == ETIMEDOUT
                                   ? "timeout" : future_strerror (f, errno));
    if (!optparse_hasopt (p, "quiet") || pending > 0)
        printf ("%d pending jobs\n", pending);
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
