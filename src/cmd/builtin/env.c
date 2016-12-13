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
#include "src/common/libutil/environment.h"

static void print_environment (struct environment *env)
{
    const char *val;
    for (val = environment_first (env); val; val = environment_next (env))
        printf("export %s=\"%s\"\n", environment_cursor (env), val);
    fflush(stdout);
}

static int cmd_env (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    if (av && av[n]) {
        execvp (av[n], av+n); /* no return if sucessful */
        log_err_exit ("execvp (%s)", av[n]);
    } else {
        struct environment *env = optparse_get_data (p, "env");
        if (env == NULL)
            log_msg_exit ("flux-env: failed to get flux envirnoment!");
        print_environment (env);
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
        0,
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
