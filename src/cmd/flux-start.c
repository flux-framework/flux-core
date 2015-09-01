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
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/optparse.h"
#include "src/common/libutil/cleanup.h"

int start_direct (optparse_t p, const char *cmd);
void start_slurm (optparse_t p, const char *cmd);

const int default_size = 1;

/* Workaround for shutdown bug:
 * Wait this many seconds after exit of first broker before kill -9 of
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
    { .name = "broker-opts",.key = 'o', .has_arg = 1, .arginfo = "OPTS",
      .usage = "Add comma-separated broker options, e.g. \"-o,-q\"", },
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
    int status = 0;
    char *command = NULL;
    size_t len = 0;
    optparse_t p;

    log_init ("flux-start");

    p = optparse_create ("flux-start");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if (optparse_set (p, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_set usage");
    if ((optind = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);
    if (optind < argc) {
        if (argz_create (argv + optind, &command, &len) < 0)
            oom ();
        argz_stringify (command, len, ' ');
    }

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
        status = start_direct (p, command);

    if (command)
        free (command);

    optparse_destroy (p);
    log_fini ();
    return status;
}

static int child_report (optparse_t p, pid_t pid, int rank, int status)
{
    int rc = 0;
    if (WIFEXITED (status)) {
        rc = WEXITSTATUS (status);
        if (rc == 0) {
            if (optparse_hasopt (p, "verbose"))
                msg ("%d (pid %d) exited normally", rank, pid);
        } else
            msg ("%d (pid %d) exited with rc=%d", rank, pid, rc);
    } else if (WIFSIGNALED (status)) {
        int sig = WTERMSIG (status);
        /*
         *  Set exit code to 128 + signal number, but ignore SIGKILL
         *   because that signal is used by flux-start itself, and
         *   may not indicate failure:
         */
        if (sig != 9)
            rc = 128 + sig;
        msg ("%d (pid %d) %s", rank, pid, strsignal (sig));
    } else {
        msg ("%d (pid %d) wait status=%d", rank, pid, status);
        if (status < 256)
            rc = status;
    }
    return rc;
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

void add_arg (char **argz, size_t *argz_len, const char *fmt, ...)
{
    va_list ap;
    char *arg;

    va_start (ap, fmt);
    arg = xvasprintf (fmt, ap);
    va_end (ap);
    if (argz_add (argz, argz_len, arg) < 0)
        oom ();
}

void add_args_sep (char **argz, size_t *argz_len, const char *s, int sep)
{
    char *az = NULL;
    size_t az_len = 0;
    if (argz_create_sep (s, sep, &az, &az_len) < 0)
        oom ();
    if (argz_append (argz, argz_len, az, az_len) < 0)
        oom ();
    if (az)
        free (az);
}

char *args_str (char *argz, size_t argz_len)
{
    char *cpy = xzmalloc (argz_len);
    memcpy (cpy, argz, argz_len);
    argz_stringify (cpy, argz_len, ' ');
    return cpy;
}

char *create_socket_dir (const char *sid)
{
    char *tmpdir = getenv ("TMPDIR");
    char *sockdir = xasprintf ("%s/flux-%s-XXXXXX",
                               tmpdir ? tmpdir : "/tmp", sid);

    if (!mkdtemp (sockdir))
        err_exit ("mkdtemp %s", sockdir);
    cleanup_push_string (cleanup_directory, sockdir);
    return sockdir;
}

int start_direct (optparse_t p, const char *cmd)
{
    int size = optparse_get_int (p, "size", default_size);
    const char *broker_opts = optparse_get_str (p, "broker-opts", NULL);
    char *broker_path = getenv ("FLUX_BROKER_PATH");
    bool child_killer_armed = false;;
    int rank;
    pid_t *pids;
    int reaped = 0;
    int rc = 0;
    char *sid = xasprintf ("%d", getpid ());
    char *sockdir = create_socket_dir (sid);

    if (!broker_path)
        msg_exit ("FLUX_BROKER_PATH is not set");

    pids = xzmalloc (size * sizeof (pids[0]));
    for (rank = 0; rank < size; rank++) {
        char *argz = NULL;
        size_t argz_len = 0;
        char **av = NULL;

        add_arg (&argz, &argz_len, "%s", broker_path);
        add_arg (&argz, &argz_len, "--size=%d", size);
        add_arg (&argz, &argz_len, "--rank=%d", rank);
        add_arg (&argz, &argz_len, "--sid=%s", sid);
        add_arg (&argz, &argz_len, "--socket-directory=%s", sockdir);
        if (broker_opts)
            add_args_sep (&argz, &argz_len, broker_opts, ',');
        if (rank == 0 && cmd)
            add_arg (&argz, &argz_len, "--command=%s", cmd);
        if (optparse_hasopt (p, "verbose")) {
            char *s = args_str (argz, argz_len);
            msg ("%d: %s", rank, s);
            free (s);
        }
        av = xzmalloc (sizeof (char *) * (argz_count (argz, argz_len) + 1));
        argz_extract (argz, argz_len, av);
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
        if (av)
            free (av);
        if (argz)
            free (argz);
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
                    int code = child_report (p, pid, rank, status);
                    if (rc < code)
                        rc = code;
                    pids[rank] = -1;
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

    free (sid);
    free (pids);
    free (sockdir);
    return (rc);
}

void start_slurm (optparse_t p, const char *cmd)
{
    int size = optparse_get_int (p, "size", default_size);
    const char *broker_opts = optparse_get_str (p, "broker-opts", NULL);
    int nnodes = optparse_get_int (p, "nnodes", size);
    const char *partition = optparse_get_str (p, "partition", NULL);
    char *broker_path = getenv ("FLUX_BROKER_PATH");
    char *srun_path = "/usr/bin/srun";
    char *argz = NULL;
    size_t argz_len = 0;
    char **av = NULL;

    if (nnodes > size)
        size = nnodes;
    if (!broker_path)
        msg_exit ("FLUX_BROKER_PATH is not set");

    add_arg (&argz, &argz_len, "%s", srun_path);
    add_arg (&argz, &argz_len, "--nodes=%d", nnodes);
    add_arg (&argz, &argz_len, "--ntasks=%d", size);
    //add_arg (&argz, &argz_len, "--overcommit");
    add_arg (&argz, &argz_len, "--propagate=CORE");
    if (!cmd)
        add_arg (&argz, &argz_len, "--pty");
    add_arg (&argz, &argz_len, "--job-name=%s", "flux");
    if (partition)
        add_arg (&argz, &argz_len, "--partition=%s", partition);
    add_arg (&argz, &argz_len, "--mpi=none");

    add_arg (&argz, &argz_len, "%s", broker_path);
    add_arg (&argz, &argz_len, "--pmi-boot");
    //add_arg (&argz, &argz_len, "--logdest=%s", "broker.log");
    if (broker_opts)
        add_args_sep (&argz, &argz_len, broker_opts, ',');
    if (cmd)
        add_arg (&argz, &argz_len, "--command=%s", cmd);

    if (optparse_hasopt (p, "verbose")) {
        char *s = args_str (argz, argz_len);
        msg ("%s", s);
        free (s);
    }
    av = xzmalloc (sizeof (char *) * (argz_count (argz, argz_len) + 1));
    argz_extract (argz, argz_len, av);
    if (!optparse_hasopt (p, "noexec")) {
        if (execv (av[0], av) < 0)
            err_exit ("execv %s", av[0]);
    }
    if (av)
        free (av);
    if (argz)
        free (argz);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
