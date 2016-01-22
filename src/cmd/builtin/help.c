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
#include "src/common/libutil/setenvf.h"
#include "builtin.h"

static int cmd_help (optparse_t *p, int ac, char *av[])
{
    flux_conf_t cf;
    int n = optparse_optind (p);
    char *cmd;

    if (!(cf = optparse_get_data (p, "conf")))
        msg_exit ("flux-help: error getting flux_conf_t");

    if (n < ac) {
        const char *cf_path = flux_conf_get (cf, "general.man_path");
        const char *topic = av [n];
        if (cf_path)
            setenvf ("MANPATH", 1, "%s:%s", cf_path, MANDIR);
        else
            setenv ("MANPATH", MANDIR, 1);
        cmd = xasprintf ("man flux-%s %s", topic, topic);
        if (system (cmd) < 0)
            err_exit ("man");
        free (cmd);
    } else
        usage (optparse_get_parent (p));
    return (0);
}

int subcommand_help_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "help",
        cmd_help,
        "[OPTIONS...] [COMMAND...]",
        "Display help information for flux commands",
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
