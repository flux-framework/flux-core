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
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/argv.h"
#include "src/common/libutil/log.h"

typedef enum { START_DIRECT, START_SLURM, START_SCREEN } method_t;

void start_direct (int size, const char *cmd, bool verbose);

#define OPTIONS "hm:s:v"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"verbose",    no_argument,        0, 'v'},
    {"method",     required_argument,  0, 'm'},
    {"size",       required_argument,  0, 's'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, "Usage: flux-start [OPTIONS] command ...\n"
"where options are:\n"
"  -m,--method METHOD    start with slurm, screen, or direct (default direct)\n"
"  -s,--size N           start N ranks\n"
"  -v,--verbose          be chatty\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    char *command = NULL;
    method_t method = START_DIRECT;
    int size = 1;
    bool vopt = false;

    log_init ("flux-start");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'v': /* --verbose */
                vopt = true;
                break;
            case 'm': /* --method METHOD */
                if (!strcmp (optarg, "slurm"))
                    method = START_SLURM;
                else if (!strcmp (optarg, "screen"))
                    method = START_SCREEN;
                else if (!strcmp (optarg, "direct"))
                    method = START_DIRECT;
                else
                    usage ();
                break;
            case 's': /* --size N */
                size = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind < argc)
        command = argv_concat (argc - optind, argv + optind, " ");

    switch (method) {
        case START_DIRECT:
            start_direct (size, command, vopt);
            break;
        case START_SLURM:
        case START_SCREEN:
            msg_exit ("not implemented yet");
    }


    if (command)
        free (command);

    log_fini ();
    return 0;
}

void start_direct (int size, const char *cmd, bool verbose)
{
    char *cmbd_path = getenv ("FLUX_CMBD_PATH");
    int rank;
    pid_t *pids;

    pids = xzmalloc (size * sizeof (pids[0]));
    for (rank = 0; rank < size; rank++) {
        int ac;
        char **av;

        argv_create (&ac, &av);
        argv_push (&ac, &av, "%s", cmbd_path ? cmbd_path : CMBD_PATH);
        argv_push (&ac, &av, "--size=%d", size);
        argv_push (&ac, &av, "--rank=%d", rank);
        if (rank == 0 && cmd)
            argv_push (&ac, &av, "--command=%s", cmd);

        if (verbose) {
            char *s = argv_concat (ac, av, " ");
            msg ("%d: %s", rank, s);
            free (s);
        }

        switch ((pids[rank] = fork ())) {
            case -1:
                err_exit ("fork");
            case 0: /* child */
                if (execv (av[0], av) < 0)
                    err_exit ("execv %s", av[0]);
                break;
            default: /* parent */
                break;
        }
        argv_destroy (ac, av);
    }
    (void)close (STDIN_FILENO);

    for (rank = 0; rank < size; rank++) {
        int status, rc;
        if (waitpid (pids[rank], &status, 0) < 0)
            err_exit ("waitpid %u (rank %d)", pids[rank], rank);
        if (WIFEXITED (status)) {
            rc = WEXITSTATUS (status);
            if (rc == 0) {
                if (verbose)
                    msg ("%d (pid %d) exited normally", rank, pids[rank]);
            } else
                msg ("%d (pid %d) exited with rc=%d", rank, pids[rank], rc);
        } else if (WIFSIGNALED (status)) {
            msg ("%d (pid %d) %s", rank, pids[rank],
                 strsignal (WTERMSIG (status)));
        } else {
            msg ("%d (pid %d) wait status=%d", rank, pids[rank], status);
        }
    }
    free (pids);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
