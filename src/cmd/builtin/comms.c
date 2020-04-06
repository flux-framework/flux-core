/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "builtin.h"

#include <inttypes.h>
#include <argz.h>

static int internal_comms_info (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    uint32_t rank, size;
    const char *arity;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (flux_get_rank (h, &rank) < 0)
        log_err_exit ("flux_get_rank");
    if (flux_get_size (h, &size) < 0)
        log_err_exit ("flux_get_size");
    if (!(arity = flux_attr_get (h, "tbon.arity")))
        log_err_exit ("flux_attr_get tbon.arity");
    printf ("rank=%"PRIu32"\n", rank);
    printf ("size=%"PRIu32"\n", size);
    printf ("arity=%s\n", arity);

    flux_close (h);
    return 0;
}

static int internal_comms_panic (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    char *argz = NULL;
    size_t argz_len = 0;
    error_t e;
    flux_t *h;

    if (optindex < ac) {
        e = argz_create (av + optindex, &argz, &argz_len);
        if (e != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (argz, argz_len, ' ');
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (flux_panic (h, FLUX_NODEID_ANY, 0, argz ? argz : "user request") < 0)
        log_err_exit ("flux_panic");
    free (argz);

    flux_close (h);
    return 0;
}

static int internal_comms_idle (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f;
    const char *peers;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "cmb.lspeer", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_rpc_get (f, &peers) < 0)
        log_err_exit ("cmb.lspeer");
    printf ("%s\n", peers);
    flux_future_destroy (f);

    flux_close (h);
    return 0;
}

int cmd_comms (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-comms");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_subcommand comms_subcmds[] = {
    { "idle",
      "",
      "List idle times of broker peers",
      internal_comms_idle,
      0,
      NULL,
    },
    { "info",
      "",
      "List rank, size, TBON branching factor",
      internal_comms_info,
      0,
      NULL,
    },
    { "panic",
      "[msg ...]",
      "Tell broker to print message and call _exit(1)",
      internal_comms_panic,
      0,
      NULL,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_comms_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "comms", cmd_comms, NULL, "Manage broker communications", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "comms"),
                                  comms_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
