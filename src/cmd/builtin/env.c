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
#include <unistd.h>

#include "builtin.h"

static void print_environment (flux_conf_t *cf)
{
    const char *key, *value;
    for (value = (char*)flux_conf_environment_first (cf),
         key = (char*)flux_conf_environment_cursor(cf);
         value != NULL;
         value = flux_conf_environment_next(cf), key = flux_conf_environment_cursor(cf)) {
        printf ("export %s=\"%s\"\n", key, value);
    }
    fflush (stdout);
}

static int cmd_env (optparse_t *p, int ac, char *av[])
{
    int n = optparse_optind (p);
    if (av && av[n]) {
        execvp (av[n], av+n); /* no return if sucessful */
        err_exit ("execvp (%s)", av[n]);
    } else {
        flux_conf_t *cf = optparse_get_data (p, "conf");
        if (cf == NULL)
            msg_exit ("flux-env: failed to get flux config!");
        print_environment (cf);
    }
    return (0);
}

int subcommand_env_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "env",
        cmd_env,
        "[OPTIONS...] [COMMAND...]",
        "Print the flux environment or execute COMMAND inside it",
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
