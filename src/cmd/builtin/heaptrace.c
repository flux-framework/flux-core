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

static int internal_heaptrace_start (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f;

    if (optparse_option_index (p) != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_pack (h, "heaptrace.start", FLUX_NODEID_ANY, 0,
                             "{ s:s }", "filename", av[ac - 1]))
            || flux_future_get (f, NULL) < 0)
        log_err_exit ("heaptrace.start");
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int internal_heaptrace_stop (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "heaptrace.stop", NULL, FLUX_NODEID_ANY, 0))
            || flux_rpc_get (f, NULL) < 0)
        log_err_exit ("heaptrace.stop");
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int internal_heaptrace_dump (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f;

    if (optparse_option_index (p) != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_pack (h, "heaptrace.dump", FLUX_NODEID_ANY, 0,
                             "{ s:s }", "reason", av[ac - 1]))
            || flux_rpc_get (f , NULL) < 0)
        log_err_exit ("heaptrace.dump");
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_heaptrace (optparse_t *p, int ac, char *av[])
{
    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_subcommand heaptrace_subcmds[] = {
    { "start",
      "FILENAME",
      "start heap profiling, sending output to FILENAME",
      internal_heaptrace_start,
      0,
      NULL,
    },
    { "stop",
      NULL,
      "stop heap profiling",
      internal_heaptrace_stop,
      0,
      NULL,
    },
    { "dump",
      "REASON",
      "dump heap profile",
      internal_heaptrace_dump,
      0,
      NULL,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_heaptrace_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p, "heaptrace", cmd_heaptrace, NULL,
            "Control google-perftools heap profiling of flux-broker", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "heaptrace"),
                                  heaptrace_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
