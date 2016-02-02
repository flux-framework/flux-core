/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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

static void config_hwloc_paths (flux_t h, const char *dirpath)
{
    uint32_t size, rank;
    const char *key_prefix = "config.resource.hwloc.xml";
    char key[64];
    char path[PATH_MAX];
    int n;

    if (flux_get_size (h, &size) < 0)
        err_exit ("flux_get_size");
    for (rank = 0; rank < size; rank++) {
        n = snprintf (path, sizeof (path), "%s/%"PRIu32".xml", dirpath, rank);
        assert (n < sizeof (path));
        if (access (path, R_OK) < 0)
            err_exit ("%s", path);
        n = snprintf (key, sizeof (key), "%s.%"PRIu32, key_prefix, rank);
        assert (n < sizeof (key));
        if (kvs_put_string (h, key, path) < 0)
            err_exit ("kvs_put_string");
    }
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
}

static void request_hwloc_reload (flux_t h, const char *nodeset)
{
    flux_rpc_t *rpc;

    if (!(rpc = flux_rpc_multi (h, "resource-hwloc.reload", NULL,
                                                        nodeset, 0)))
        err_exit ("flux_rpc_multi");
    while (!flux_rpc_completed (rpc)) {
        const char *json_str;
        uint32_t nodeid = FLUX_NODEID_ANY;
        if (flux_rpc_get (rpc, &nodeid, &json_str) < 0) {
            if (nodeid == FLUX_NODEID_ANY)
                err ("flux_rpc_get");
            else
                err ("rpc(%"PRIu32")", nodeid);
        }
    }
    flux_rpc_destroy (rpc);
}

static int internal_hwloc_reload (optparse_t *p, int ac, char *av[])
{
    int n = optparse_optind (p);
    const char *default_nodeset = "all";
    const char *nodeset = optparse_get_str (p, "rank", default_nodeset);
    char *dirpath;
    flux_t h;

    if (n != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        err_exit ("flux_open");
    if (!(dirpath = realpath (av[n], NULL)))
        err_exit ("%s", av[n]);

    config_hwloc_paths (h, dirpath);
    request_hwloc_reload (h, nodeset);

    free (dirpath);
    flux_close (h);
    return (0);
}

int cmd_hwloc (optparse_t *p, int ac, char *av[])
{
    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option reload_opts[] = {
    { .name = "rank",  .key = 'r',  .has_arg = 1,
      .usage = "Target specified nodeset, or \"all\" (default)", },
    OPTPARSE_TABLE_END,
};

static struct optparse_subcommand hwloc_subcmds[] = {
    { "reload",
      "[OPTIONS] DIR",
      "Reload XML from DIR/<rank>.xml files",
      internal_hwloc_reload,
      reload_opts,
    },
    OPTPARSE_SUBCMD_END,
};

int subcommand_hwloc_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p, "hwloc", cmd_hwloc, NULL,
                                "Control/query resource-hwloc service", NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "hwloc"),
                                  hwloc_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
