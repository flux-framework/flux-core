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

void start_direct (int size, int kary, const char *modules, const char *modopt,
                   const char *cmd, bool verbose, bool noexec);

/* For START_DIRECT:  wait this long after exit of first cmbd before
 * sending signals to those that remain.  This is a temporary work-around
 * for unreliable shutdown in fast cycling comms sessions.
 */
const int child_wait_seconds = 1;

#define OPTIONS "hvm:s:k:M:O:X"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"verbose",    no_argument,        0, 'v'},
    {"noexec",     no_argument,        0, 'X'},
    {"method",     required_argument,  0, 'm'},
    {"size",       required_argument,  0, 's'},
    {"k-ary",      required_argument,  0, 'k'},
    {"modules",    required_argument,  0, 'M'},
    {"modopt",     required_argument,  0, 'O'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, "Usage: flux-start [OPTIONS] command ...\n"
"where options are:\n"
"  -m,--method METHOD      use slurm/screen/direct (default: direct)\n"
"  -s,--size N             set number of ranks in session\n"
"  -k,--k-ary N            wire up in k-ary tree\n"
"  -M,--modules modules    load the named modules\n"
"  -O,--modopt options     set module options\n"
"  -X,--noexec             don't execute (useful with -v)\n"
"  -v,--verbose            be annoyingly informative\n"
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
    bool Xopt = false;
    int kary = -1;
    char *modules = NULL;
    char *modopt = NULL;

    log_init ("flux-start");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'v': /* --verbose */
                vopt = true;
                break;
            case 'X': /* --noexec */
                Xopt = true;
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
            case 'k': /* --k-ary-N */
                kary = strtoul (optarg, NULL, 10);
                break;
            case 'M': /* --modules name[,name] */
                modules = optarg;
                break;
            case 'O': /* --modopt "mod:key=val [mod:key=val...]" */
                modopt = optarg;
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
            start_direct (size, kary, modules, modopt, command, vopt, Xopt);
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

static void child_report (pid_t pid, int rank, int status, bool verbose)
{
    int rc;
    if (WIFEXITED (status)) {
        rc = WEXITSTATUS (status);
        if (rc == 0) {
            if (verbose)
                msg ("%d (pid %d) exited normally", rank, pid);
        } else
            msg ("%d (pid %d) exited with rc=%d", rank, pid, rc);
    } else if (WIFSIGNALED (status))
        msg ("%d (pid %d) %s", rank, pid, strsignal (WTERMSIG (status)));
    else
        msg ("%d (pid %d) wait status=%d", rank, pid, status);
}

/* XXX See note above child_wait_seconds at top of program.
 */
static volatile sig_atomic_t child_killer_timeout;
void alarm_handler (int a)
{
    child_killer_timeout = 1;
}

void child_killer_arm (void)
{
    struct sigaction sa;
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = alarm_handler;
    sigaction (SIGALRM, &sa, NULL);
    alarm (child_wait_seconds);
    child_killer_timeout = 0;
}

void child_killer (pid_t *pids, int size, bool verbose)
{
    int rank;
    if (child_killer_timeout) {
        for (rank = 0; rank < size; rank++) {
            if (pids[rank] > 0) {
                if (verbose)
                    msg ("%d: kill -9 %u", rank, pids[rank]);
                (void)kill (pids[rank], SIGKILL);
            }
        }
    }
}

void start_direct (int size, int kary, const char *modules, const char *modopt,
                   const char *cmd, bool verbose, bool noexec)
{
    bool child_killer_armed = false;;
    char *cmbd_path = getenv ("FLUX_CMBD_PATH");
    int rank;
    pid_t *pids;
    int reaped = 0;

    pids = xzmalloc (size * sizeof (pids[0]));
    for (rank = 0; rank < size; rank++) {
        int ac;
        char **av;

        argv_create (&ac, &av);
        argv_push (&ac, &av, "%s", cmbd_path ? cmbd_path : CMBD_PATH);
        argv_push (&ac, &av, "--size=%d", size);
        argv_push (&ac, &av, "--rank=%d", rank);
        if (kary != -1)
            argv_push (&ac, &av, "--k-ary=%d", kary);
        if (modules)
            argv_push (&ac, &av, "--modules=%s", modules);
        if (rank == 0 && cmd)
            argv_push (&ac, &av, "--command=%s", cmd);
        if (modopt)
            argv_push_cmdline (&ac, &av, modopt);
        if (verbose) {
            char *s = argv_concat (ac, av, " ");
            msg ("%d: %s", rank, s);
            free (s);
        }
        if (!noexec) {
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
        }
        argv_destroy (ac, av);
    }
    if (!noexec) {
        (void)close (STDIN_FILENO);
        while (reaped < size) {
            int status;
            pid_t pid;

            if ((pid = wait (&status)) < 0) {
                if (errno == EINTR) {
                    if (child_killer_armed)
                        child_killer (pids, size, verbose);
                    continue;
                }
                err_exit ("wait");
            }
            for (rank = 0; rank < size; rank++) {
                if (pids[rank] == pid) {
                    pids[rank] = -1;
                    child_report (pid, rank, status, verbose);
                    if (!child_killer_armed) {
                        child_killer_arm ();
                        child_killer_armed = true;
                    }
                    reaped++;
                    break;
                }
            }
        }
    }

    free (pids);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
