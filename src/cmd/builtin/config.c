/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "builtin.h"

static int internal_config_reload (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f = NULL;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "config.reload", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("error constructing config.reload RPC");
    if (flux_future_get (f, NULL) < 0)
        log_msg_exit ("reload: %s", flux_future_error_string (f));
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_config (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-config");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_subcommand config_subcmds[] = {
    { "reload",
      "[OPTIONS]",
      "Reload configuration from files",
      internal_config_reload,
      0,
      NULL,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_config_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "config", cmd_config, NULL, "Manage configuration", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "config"),
                                  config_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
