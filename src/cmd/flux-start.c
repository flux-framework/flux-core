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
#include <sys/time.h>
#include <sys/resource.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/argv.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/optparse.h"

void start_direct (optparse_t p, const char *cmd);
void start_slurm (optparse_t p, const char *cmd);

const int default_size = 1;

/* Workaround for shutdown bug:
 * Wait this many seconds after exit of first cmbd before kill -9 of
 * those that remain.  This is a temporary work-around for unreliable
 * shutdown in fast cycling comms sessions.
 */
const int child_wait_seconds = 1;

const char *usage_msg = "[OPTIONS] command ...";
static struct optparse_option opts[] = {
    { .name = "verbose",    .key = 'v', .has_arg = 0,
      .usage = "Be annoyingly informative", },
    { .name = "noexec",     .key = 'X', .has_arg = 0,
      .usage = "Don't execute (useful with -v, --verbose)", },
    { .name = "size",       .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set number of ranks in session", },
    { .name = "cmbd-opts",  .key = 'o', .has_arg = 1, .arginfo = "OPTS",
      .usage = "Add comma-separated cmbd options, e.g. \"-o,-q\"", },
    { .name = "nnodes",     .key = 'N', .has_arg = 1, .arginfo = "N",
      .usage = "Set number of nodes (implies SLURM)", },
    { .name = "partition",  .key = 'p', .has_arg = 1, .arginfo = "NAME",
      .usage = "Select partition (implies SLURM)", },
    { .name = "slurm",      .key = 'S', .has_arg = 0,
      .usage = "Launch with SLURM", },
    OPTPARSE_TABLE_END,
};

int main (int argc, char *argv[])
{
    char *command = NULL;
    optparse_t p;

    log_init ("flux-start");

    p = optparse_create ("flux-start");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if (optparse_set (p, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_set usage");
    if ((optind = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);
    if (optind < argc)
        command = argv_concat (argc - optind, argv + optind, " ");

    /* Allow unlimited core dumps.
     */
    struct rlimit rl;
    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY;
    if (setrlimit (RLIMIT_CORE, &rl) < 0)
        err ("setrlimit: could not remove core file size limit");

    if (optparse_hasopt (p, "slurm") || optparse_hasopt (p, "nnodes")
                                     || optparse_hasopt (p, "partition"))
        start_slurm (p, command);
    else
        start_direct (p, command);

    if (command)
        free (command);

    optparse_destroy (p);
    log_fini ();
    return 0;
}

static void child_report (optparse_t p, pid_t pid, int rank, int status)
{
    int rc;
    if (WIFEXITED (status)) {
        rc = WEXITSTATUS (status);
        if (rc == 0) {
            if (optparse_hasopt (p, "verbose"))
                msg ("%d (pid %d) exited normally", rank, pid);
        } else
            msg ("%d (pid %d) exited with rc=%d", rank, pid, rc);
    } else if (WIFSIGNALED (status))
        msg ("%d (pid %d) %s", rank, pid, strsignal (WTERMSIG (status)));
    else
        msg ("%d (pid %d) wait status=%d", rank, pid, status);
}

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

void child_killer (optparse_t p, pid_t *pids, int size)
{
    int rank;
    if (child_killer_timeout) {
        for (rank = 0; rank < size; rank++) {
            if (pids[rank] > 0) {
                if (optparse_hasopt (p, "verbose"))
                    msg ("%d: kill -9 %u", rank, pids[rank]);
                (void)kill (pids[rank], SIGKILL);
            }
        }
    }
}

void push_extra_args (int *ac, char ***av, const char *opts)
{
    char *cpy = xstrdup (opts);
    char *opt, *saveptr = NULL, *a1 = cpy;
    while ((opt = strtok_r (a1, ",", &saveptr))) {
        argv_push (ac, av, "%s", opt);
        a1 = NULL;
    }
    free (cpy);
}

void start_direct (optparse_t p, const char *cmd)
{
    int size = optparse_get_int (p, "size", default_size);
    const char *cmbd_opts = optparse_get_str (p, "cmbd-opts", NULL);
    char *cmbd_path = getenv ("FLUX_CMBD_PATH");
    bool child_killer_armed = false;;
    int rank;
    pid_t *pids;
    int reaped = 0;

    if (!cmbd_path)
        msg_exit ("FLUX_CMBD_PATH is not set");

    pids = xzmalloc (size * sizeof (pids[0]));
    for (rank = 0; rank < size; rank++) {
        int ac;
        char **av;

        argv_create (&ac, &av);
        argv_push (&ac, &av, "%s", cmbd_path);
        argv_push (&ac, &av, "--size=%d", size);
        argv_push (&ac, &av, "--rank=%d", rank);
        if (cmbd_opts)
            push_extra_args (&ac, &av, cmbd_opts);
        if (rank == 0 && cmd)
            argv_push (&ac, &av, "--command=%s", cmd);
        if (optparse_hasopt (p, "verbose")) {
            char *s = argv_concat (ac, av, " ");
            msg ("%d: %s", rank, s);
            free (s);
        }
        if (!optparse_hasopt (p, "noexec")) {
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
    if (!optparse_hasopt (p, "noexec")) {
        (void)close (STDIN_FILENO);
        while (reaped < size) {
            int status;
            pid_t pid;

            if ((pid = wait (&status)) < 0) {
                if (errno == EINTR) {
                    if (child_killer_armed)
                        child_killer (p, pids, size);
                    continue;
                }
                err_exit ("wait");
            }
            for (rank = 0; rank < size; rank++) {
                if (pids[rank] == pid) {
                    pids[rank] = -1;
                    child_report (p, pid, rank, status);
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

void start_slurm (optparse_t p, const char *cmd)
{
    int size = optparse_get_int (p, "size", default_size);
    const char *cmbd_opts = optparse_get_str (p, "cmbd-opts", NULL);
    int nnodes = optparse_get_int (p, "nnodes", size);
    const char *partition = optparse_get_str (p, "partition", NULL);
    char *cmbd_path = getenv ("FLUX_CMBD_PATH");
    char *srun_path = "/usr/bin/srun";
    int ac;
    char **av;

    if (nnodes > size)
        size = nnodes;
    if (!cmbd_path)
        msg_exit ("FLUX_CMBD_PATH is not set");

    argv_create (&ac, &av);
    argv_push (&ac, &av, "%s", srun_path);
    argv_push (&ac, &av, "--nodes=%d", nnodes);
    argv_push (&ac, &av, "--ntasks=%d", size);
    //argv_push (&ac, &av, "--overcommit");
    argv_push (&ac, &av, "--propagate=CORE");
    if (!cmd)
        argv_push (&ac, &av, "--pty");
    argv_push (&ac, &av, "--job-name=%s", "flux");
    if (partition)
        argv_push (&ac, &av, "--partition=%s", partition);

    argv_push (&ac, &av, "%s", cmbd_path);
    argv_push (&ac, &av, "--pmi-boot");
    argv_push (&ac, &av, "--logdest=%s", "cmbd.log");
    if (cmbd_opts)
        push_extra_args (&ac, &av, cmbd_opts);
    if (cmd)
        argv_push (&ac, &av, "--command=%s", cmd);

    if (optparse_hasopt (p, "verbose")) {
        char *s = argv_concat (ac, av, " ");
        msg ("%s", s);
        free (s);
    }
    if (!optparse_hasopt (p, "noexec")) {
        if (execv (av[0], av) < 0)
            err_exit ("execv %s", av[0]);
    }
    argv_destroy (ac, av);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
