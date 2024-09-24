/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* lptest.c - ripple test */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>

#include "builtin.h"

static void lptest (int length, int count)
{
    int i;
    int j;

    for (i = 0; i < count; i++) {
        for (j = 0; j < length; j++)
            putchar (0x21 + ((i + j) % 0x5e)); // charset: !(0x21) thru ~(0x7e)
        putchar ('\n');
    }
}

static int cmd_lptest (optparse_t *p, int ac, char **av)
{
    int n = optparse_option_index (p);
    int length = 79;
    int count = 200;
    char *endptr;

    log_init ("flux-lptest");

    if (n < ac) {
        errno = 0;
        length = strtoul (av[n++], &endptr, 10);
        if (errno != 0 || *endptr != '\0')
            log_msg_exit ("error parsing length");
    }
    if (n < ac) {
        errno = 0;
        count = strtoul (av[n++], &endptr, 10);
        if (errno != 0 || *endptr != '\0')
            log_msg_exit ("error parsing count");
    }
    if (n < ac)
        optparse_fatal_usage (p, 1, NULL);
    lptest (length, count);
    return 0;
}

int subcommand_lptest_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "lptest",
        cmd_lptest,
        "[length] [count]",
        "Create lines of regular output for standard I/O testing",
        0,
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi:ts=4 sw=4 expandtab
