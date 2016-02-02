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

static int cmd_broker (optparse_t *p, int ac, char *av[])
{
    const char *path;
    flux_conf_t cf;
    if (!(cf = optparse_get_data (p, "conf")))
        return (-1);
    path = flux_conf_environment_get (cf, "FLUX_BROKER_PATH");
    execvp (path, av); /* no return if successful */
    return (-1);
}

int subcommand_broker_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "broker",
        cmd_broker,
        "[OPTIONS...] [COMMAND...]",
        "Run the flux broker",
        NULL);

    if (e != OPTPARSE_SUCCESS)
        return (-1);

    /* Do not parse options before calling cmd_broker:
     */
    optparse_set (optparse_get_subcommand (p, "broker"),
                  OPTPARSE_SUBCMD_NOOPTS, 1);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
