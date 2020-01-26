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
int cmd_status (optparse_t *p, int argc, char **argv);

int stdin_flags;

static struct optparse_option global_opts[] =  {
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
    { "status",
      "[OPTIONS]",
      "Get queue status",
      cmd_status,
      0,
      NULL,
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
    log_msg ("Job submission is %s%s%s",
             enable ? "enabled" : "disabled",
             enable ? "" : ", reason: ",
             enable ? "" : reason);
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
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
