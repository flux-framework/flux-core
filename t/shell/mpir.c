/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* mpir.c - test for shell mpir/ptrace plugins */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/shell/mpir/proctable.h"
#include "src/cmd/job/mpir.h"

extern struct proctable *proctable;
extern MPIR_PROCDESC *MPIR_proctable;
extern int MPIR_proctable_size;

extern char MPIR_executable_path[256];
extern char MPIR_server_arguments[1024];

static struct optparse_option opts[] = {
    { .name = "leader-rank",
      .key = 'r',
      .has_arg = 1,
      .arginfo = "RANK",
      .usage = "specify shell leader rank"
    },
    { .name = "service",
      .key = 's',
      .has_arg = 1,
      .arginfo = "NAME",
      .usage = "specify shell service NAME"
    },
    { .name = "tool-launch",
      .key = 't',
      .has_arg = 0,
      .usage = "test tool launch via MPIR_executable_path",
    },
    { .name = "send-sigcont",
      .key = 'S',
      .has_arg = 1,
      .arginfo = "ID",
      .usage = "send SIGCONT to job ID after tool launch",
    },
    OPTPARSE_TABLE_END
};

static void print_proctable (void)
{
    json_t *o = proctable_to_json (proctable);
    char *s = json_dumps (o, 0);
    fprintf (stderr, "proctable=%s\n", s);
    json_decref (o);
    free (s);
}

int main (int ac, char **av)
{
    int rank;
    flux_t *h = NULL;
    const char *service;
    const char *jobid;
    optparse_t *p;
    int optindex;

    log_init ("mpir-test");
    if (!(p = optparse_create ("mpir-test"))
        || optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_create");

    if ((optindex = optparse_parse_args (p, ac, av)) < 0)
        exit (1);

    rank = optparse_get_int (p, "leader-rank", -1);
    service = optparse_get_str (p, "service", NULL);
    if (rank < 0 || service == NULL)
        log_msg_exit ("--rank and --service are required");

    if (optparse_hasopt (p, "tool-launch")) {
        if (optindex == ac)
            log_msg_exit ("--tool-launch requires specification of tool args");
        /*  Set MPIR_executable_path */
        snprintf (MPIR_executable_path,
                  sizeof (MPIR_executable_path),
                  "%s",
                  av[optindex++]);
        /*  Set MPIR_server_arguments */
        int i = 0;
        while (optindex < ac) {
            int n = snprintf (MPIR_server_arguments+i,
                              sizeof (MPIR_server_arguments) - i,
                              "%s",
                              av[optindex++]);
            i += n + 1;
        }
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    mpir_setup_interface (h, 0, false, false, rank, service);
    print_proctable ();

    if ((jobid = optparse_get_str (p, "send-sigcont", NULL))) {
        flux_jobid_t id;
        flux_future_t *f;

        if (flux_job_id_parse (jobid, &id) < 0)
            log_msg_exit ("failed to parse jobid '%s'", jobid);
        if (!(f = flux_job_kill (h, id, SIGCONT))
            || flux_future_get (f, NULL) < 0)
            log_err_exit ("flux_job_kill");
        flux_future_destroy (f);
    }

    flux_reactor_run (flux_get_reactor (h), 0);

    mpir_shutdown (h);
    proctable_destroy (proctable);
    flux_close (h);
    optparse_destroy (p);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

