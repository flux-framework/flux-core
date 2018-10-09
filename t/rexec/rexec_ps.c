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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <jansson.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libsubprocess/subprocess.h"

extern char **environ;

static struct optparse_option cmdopts[] = {
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "rank",
      .usage = "Specify rank for test" },
    OPTPARSE_TABLE_END
};

void output (int rank, json_t *procs)
{
    size_t index;
    json_t *value;

    if (!json_is_array (procs))
        log_msg_exit ("procs returned is not an array");

    json_array_foreach (procs, index, value) {
        int pid;
        char *sender;

        if (json_unpack (value, "{ s:i s:s }",
                         "pid", &pid,
                         "sender", &sender) < 0)
            log_msg_exit ("json_unpack");

        printf ("%s\t%d\t%d\n", sender, rank, pid);
    }

}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *reactor;
    const char *optargp;
    int rank;
    flux_future_t *f;
    optparse_t *opts;
    int optindex;
    int resp_rank;
    json_t *resp_procs;

    log_init ("rexec_ps");

    opts = optparse_create ("rexec_ps");
    if (optparse_add_option_table (opts, cmdopts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);

    if (optparse_getopt (opts, "rank", &optargp) > 0) {
        rank = atoi (optargp);
    } else {
        optparse_print_usage (opts);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(reactor = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");

    if (!(f = flux_rpc (h, "cmb.rexec.processes", NULL, rank, 0)))
        log_err_exit ("flux_rpc");

    if (flux_rpc_get_unpack (f, "{ s:i s:o }",
                             "rank", &resp_rank,
                             "procs", &resp_procs) < 0)
        log_err_exit ("flux_rpc_get_unpack");

    if (rank != resp_rank)
        log_err_exit ("invalid rank returned = %d", resp_rank);

    output (rank, resp_procs);

    /* Clean up.
     */
    flux_close (h);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
