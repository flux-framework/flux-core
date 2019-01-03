/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "builtin.h"

static struct optparse_option dmesg_opts[] = {
    { .name = "clear",  .key = 'C',  .has_arg = 0,
      .usage = "Clear the ring buffer", },
    { .name = "read-clear",  .key = 'c',  .has_arg = 0,
      .usage = "Clear the ring buffer contents after printing", },
    { .name = "follow",  .key = 'f',  .has_arg = 0,
      .usage = "Track new entries as are logged", },
    OPTPARSE_TABLE_END,
};


static int cmd_dmesg (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    int flags = 0;
    flux_log_f print_cb = flux_log_fprint;

    if ((n = optparse_option_index (p)) != ac)
        log_msg_exit ("flux-dmesg accepts no free arguments");

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "read-clear") || optparse_hasopt (p, "clear"))
        flags |= FLUX_DMESG_CLEAR;
    if (optparse_hasopt (p, "clear"))
        print_cb = NULL;
    if (optparse_hasopt (p, "follow"))
        flags |= FLUX_DMESG_FOLLOW;
    if (flux_dmesg (h, flags, print_cb, stdout) < 0)
        log_err_exit ("flux_dmesg");
    flux_close (h);
    return (0);
}

int subcommand_dmesg_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "dmesg",
        cmd_dmesg,
        "[OPTIONS...]",
        "Print or control log ring buffer",
        0,
        dmesg_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
