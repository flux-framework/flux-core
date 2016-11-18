/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/
#include "builtin.h"

static struct optparse_option setattr_opts[] = {
    { .name = "expunge", .key = 'e', .has_arg = 0,
      .usage = "Unset the specified attribute",
    },
    OPTPARSE_TABLE_END
};

static int cmd_setattr (optparse_t *p, int ac, char *av[])
{
    int n;
    const char *name = NULL, *val = NULL;
    flux_t *h;

    log_init ("flux-setattr");

    n = optparse_optind (p);
    if (optparse_hasopt (p, "expunge") && n == ac - 1) {
        name = av[n];
    } else if (!optparse_hasopt (p, "expunge") && n == ac - 2) {
        name = av[n];
        val = av[n + 1];
    } else {
        optparse_print_usage (p);
        exit (1);
    }

    h = builtin_get_flux_handle (p);
    if (flux_attr_set (h, name, val) < 0)
        log_err_exit ("%s", av[1]);
    flux_close (h);
    return (0);
}

static struct optparse_option lsattr_opts[] = {
    { .name = "values", .key = 'v', .has_arg = 0,
      .usage = "List values with attributes",
    },
    OPTPARSE_TABLE_END
};

static int cmd_lsattr (optparse_t *p, int ac, char *av[])
{
    const char *name, *val;
    flux_t *h;

    log_init ("flux-lsatrr");

    if (optparse_optind (p) != ac)
        optparse_fatal_usage (p, 1, NULL);
    h = builtin_get_flux_handle (p);
    name = flux_attr_first (h);
    while (name) {
        if (optparse_hasopt (p, "values")) {
            val = flux_attr_get (h, name, NULL);
            printf ("%-40s%s\n", name, val ? val : "-");
        } else {
            printf ("%s\n", name);
        }
        name = flux_attr_next (h);
    }
    flux_close (h);
    return (0);
}

static int cmd_getattr (optparse_t *p, int ac, char *av[])
{
    flux_t *h = NULL;
    const char *val;
    int n, flags;

    log_init ("flux-getattr");

    n = optparse_optind (p);
    if (n != ac - 1)
        optparse_fatal_usage (p, 1, NULL);

    h = builtin_get_flux_handle (p);
    if (!(val = flux_attr_get (h, av[n], &flags)))
        log_err_exit ("%s", av[n]);
    printf ("%s\n", val);
    flux_close (h);
    return (0);
}

int subcommand_attr_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p, "setattr", cmd_setattr,
        "name value",
        "Set broker attribute",
        0,
        setattr_opts);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommand (p, "getattr", cmd_getattr,
        "name",
        "Get broker attribute",
        0,
        NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommand (p, "lsattr", cmd_lsattr,
        "[-v]",
        "List broker attributes",
        0,
        lsattr_opts);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
