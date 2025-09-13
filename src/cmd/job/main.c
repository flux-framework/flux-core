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
#include <stdio.h>
#include <locale.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"

/* Externally built subcommands */
extern int cmd_attach (optparse_t *p, int argc, char **argv);
extern struct optparse_option attach_opts[];

extern int cmd_submit (optparse_t *p, int argc, char **argv);
extern struct optparse_option submit_opts[];

extern int cmd_list (optparse_t *p, int argc, char **argv);
extern struct optparse_option list_opts[];

extern int cmd_list_inactive (optparse_t *p, int argc, char **argv);
extern struct optparse_option list_inactive_opts[];

extern int cmd_list_ids (optparse_t *p, int argc, char **argv);
extern struct optparse_option list_ids_opts[];

extern int cmd_status (optparse_t *p, int argc, char **argv);
extern struct optparse_option status_opts[];

extern int cmd_id (optparse_t *p, int argc, char **argv);
extern struct optparse_option id_opts[];

extern int cmd_namespace (optparse_t *p, int argc, char **argv);

extern int cmd_urgency (optparse_t *p, int argc, char **argv);
extern struct optparse_option urgency_opts[];

extern int cmd_raise (optparse_t *p, int argc, char **argv);
extern struct optparse_option raise_opts[];

extern int cmd_raiseall (optparse_t *p, int argc, char **argv);
extern struct optparse_option raiseall_opts[];

extern int cmd_kill (optparse_t *p, int argc, char **argv);
extern struct optparse_option kill_opts[];

extern int cmd_killall (optparse_t *p, int argc, char **argv);
extern struct optparse_option killall_opts[];

extern int cmd_eventlog (optparse_t *p, int argc, char **argv);
extern struct optparse_option eventlog_opts[];
extern int cmd_wait_event (optparse_t *p, int argc, char **argv);
extern struct optparse_option wait_event_opts[];

extern int cmd_info (optparse_t *p, int argc, char **argv);
extern struct optparse_option info_opts[];

extern int cmd_stats (optparse_t *p, int argc, char **argv);

extern int cmd_wait (optparse_t *p, int argc, char **argv);
extern struct optparse_option wait_opts[];

extern int cmd_memo (optparse_t *p, int argc, char **argv);
extern struct optparse_option memo_opts[];

extern int cmd_purge (optparse_t *p, int argc, char **argv);
extern struct optparse_option purge_opts[];

extern int cmd_taskmap (optparse_t *p, int argc, char **argv);
extern struct optparse_option taskmap_opts[];

extern int cmd_timeleft (optparse_t *p, int argc, char **argv);
extern struct optparse_option timeleft_opts[];

extern int cmd_last (optparse_t *p, int argc, char **argv);

extern int cmd_hostpids (optparse_t *p, int argc, char **argv);
extern struct optparse_option hostpids_opts[];


static struct optparse_option global_opts[] =  {
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
      "[OPTIONS] id",
      "Display eventlog for a job",
      cmd_eventlog,
      0,
      eventlog_opts
    },
    { "wait-event",
      "[OPTIONS] id event",
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
    { "hostpids",
      "[OPTIONS] JOBID",
      "Print host:pid pairs for tasks in JOBID",
      cmd_hostpids,
      0,
      hostpids_opts,
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
