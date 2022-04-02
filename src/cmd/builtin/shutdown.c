/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <flux/core.h>

#include "src/broker/state_machine.h"
#include "src/common/libutil/uri.h"

#include "builtin.h"

static void process_updates (flux_future_t *f)
{
    const char *s;

    while (flux_rpc_get_unpack (f, "{s:s}", "log", &s) == 0) {
        fprintf (stderr, "%s", s);
        flux_future_reset (f);
    }
    if (errno != ENODATA)
        log_msg_exit ("%s", future_strerror (f, errno));
}

static int subcmd (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f;
    int flags = FLUX_RPC_STREAMING;
    int optindex = optparse_option_index (p);
    bool quiet = optparse_hasopt (p, "quiet");
    int verbose = optparse_get_int (p, "verbose", 0);
    int loglevel = quiet ? LOG_WARNING
                 : verbose == 0 ? LOG_INFO : LOG_DEBUG;
    const char *target = NULL;

    log_init ("flux-shutdown");

    if (optindex < ac)
        target = av[optindex++];
    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }

    if (target) {
        char *uri = uri_resolve (target);
        if (!uri)
            log_msg_exit ("failed to resolve target %s to a Flux URI", target);
        if (!(h = flux_open (uri, 0)))
            log_err_exit ("error connecting to Flux");
        free (uri);
    }
    else {
        if (!(h = flux_open (NULL, 0)))
            log_err_exit ("error connecting to Flux");
    }

    if (optparse_hasopt (p, "background"))
        flags &= ~FLUX_RPC_STREAMING;

    /* N.B. set nodeid=FLUX_NODEID_ANY so we get immediate error from
     * broker if run on rank > 0.
     */
    if (!(f = flux_rpc_pack (h,
                             "shutdown.start",
                             FLUX_NODEID_ANY,
                             flags,
                             "{s:i}",
                             "loglevel", loglevel)))
        log_err_exit ("could not send shutdown.start request");

    if ((flags & FLUX_RPC_STREAMING))
        process_updates (f);
    else if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("%s", future_strerror (f, errno));

    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

static struct optparse_option opts[] = {
    { .name = "background", .has_arg = 0,
      .usage = "Exit the command immediately after initiating shutdown",
    },
    { .name = "quiet", .has_arg = 0,
      .usage = "Show only log messages <= LOG_WARNING level",
    },
    { .name = "verbose", .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Increase log verbosity:"
               " 0=show log messages <= LOG_INFO level (default),"
               " 1=show all log messages",
    },
    OPTPARSE_TABLE_END
};

int subcommand_shutdown_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
        "shutdown",
        subcmd,
        "[OPTIONS] [TARGET]",
        "Shut down the Flux instance",
        0,
        opts);
    if (e != OPTPARSE_SUCCESS)
        return -1;
    return 0;
}

// vi: ts=4 sw=4 expandtab
