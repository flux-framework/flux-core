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
#include <libgen.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/optparse.h"
#include "src/common/libutil/cleanup.h"
#include "src/modules/libsubprocess/subprocess.h"

int start_direct (optparse_t *p, const char *cmd);

const int default_size = 1;

struct subprocess_manager *sm;

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
      .usage = "Set number of ranks in new instance", },
    { .name = "broker-opts",.key = 'o', .has_arg = 1, .arginfo = "OPTS",
      .usage = "Add comma-separated broker options, e.g. \"-o,-q\"", },
    OPTPARSE_TABLE_END,
};

int main (int argc, char *argv[])
{
    int e, status = 0;
    char *command = NULL;
    size_t len = 0;
    optparse_t *p;

    log_init ("flux-start");

    p = optparse_create ("flux-start");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if (optparse_set (p, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_set usage");
    if ((optind = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);
    if (optind < argc) {
        if ((e = argz_create (argv + optind, &command, &len)) != 0)
            errn_exit (e, "argz_creawte");
        argz_stringify (command, len, ' ');
    }

    /* Allow unlimited core dumps.
     */
    struct rlimit rl;
    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY;
    if (setrlimit (RLIMIT_CORE, &rl) < 0)
        err ("setrlimit: could not remove core file size limit");

    status = start_direct (p, command);

    if (command)
        free (command);
    optparse_destroy (p);
    log_fini ();

    return status;
}

void alarm_handler (int a)
{
    struct subprocess *p;
    p = subprocess_manager_first (sm);
    while (p) {
        if (subprocess_pid (p))
            (void)subprocess_kill (p, SIGKILL);
        p = subprocess_manager_next (sm);
    }
}

void child_killer_arm (void)
{
    static bool armed = false;

    if (!armed) {
        struct sigaction sa;
        memset (&sa, 0, sizeof (sa));
        sa.sa_handler = alarm_handler;
        sigaction (SIGALRM, &sa, NULL);
        alarm (child_wait_seconds);
        armed = true;
    }
}

static int child_report (struct subprocess *p, void *arg)
{
    int *exit_rc = arg;
    char *rankstr = subprocess_get_context (p, "rank");
    int rank = strtol (rankstr, NULL, 10);
    int status = subprocess_exit_status (p);
    pid_t pid = subprocess_pid (p);
    int rc = 0;

    if (WIFEXITED (status)) {
        rc = WEXITSTATUS (status);
        if (rc != 0)
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
    subprocess_destroy (p);
    child_killer_arm ();
    if (*exit_rc < rc)
        *exit_rc = rc;
    free (rankstr);
    return 0;
}

void add_arg (struct subprocess *p, const char *fmt, ...)
{
    va_list ap;
    char *arg;

    va_start (ap, fmt);
    arg = xvasprintf (fmt, ap);
    va_end (ap);
    if (subprocess_argv_append (p, arg) < 0)
        err_exit ("subprocess_argv_append");
    free (arg);
}

void add_args_sep (struct subprocess *p, const char *s, int sep)
{
    char *az = NULL;
    size_t az_len = 0;
    char *arg = NULL;
    int e;

    if ((e = argz_create_sep (s, sep, &az, &az_len)) != 0)
        errn_exit (e, "argz_create_sep");
    while ((arg = argz_next (az, az_len, arg))) {
        if (subprocess_argv_append  (p, arg) < 0)
            err_exit ("subprocess_argv_append");
    }
    if (az)
        free (az);
}

char *args_str (struct subprocess *p)
{
    int i, argc = subprocess_get_argc (p);
    char *az = NULL;
    size_t az_len = 0;
    int e;

    for (i = 0; i < argc; i++)
        if ((e = argz_add (&az, &az_len, subprocess_get_arg (p, i))) != 0)
            errn_exit (e, "argz_add");
    argz_stringify (az, az_len, ' ');
    return az;
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

int start_direct (optparse_t *opts, const char *cmd)
{
    int size = optparse_get_int (opts, "size", default_size);
    const char *broker_opts = optparse_get_str (opts, "broker-opts", NULL);
    char *broker_path = getenv ("FLUX_BROKER_PATH");
    char *sid = xasprintf ("%d", getpid ());
    char *sockdir = create_socket_dir (sid);
    struct subprocess *p;
    int rank, rc = 0;

    if (!broker_path)
        msg_exit ("FLUX_BROKER_PATH is not set");

    if (!(sm = subprocess_manager_create ()))
        err_exit ("subprocess_manager_create");

    for (rank = 0; rank < size; rank++) {
        if (!(p = subprocess_create (sm)))
            err_exit ("subprocess_create");
        subprocess_set_context (p, "rank", xasprintf ("%d", rank));
        subprocess_set_callback (p, child_report, &rc);

        add_arg (p, "%s", broker_path);
        add_arg (p, "--boot-method=LOCAL");
        add_arg (p, "--size=%d", size);
        add_arg (p, "--rank=%d", rank);
        add_arg (p, "--sid=%s", sid);
        add_arg (p, "--socket-directory=%s", sockdir);
        if (broker_opts)
            add_args_sep (p, broker_opts, ',');
        if (rank == 0 && cmd)
            add_arg (p, "%s", cmd); /* must be last */

        if (optparse_hasopt (opts, "verbose")) {
            char *s = args_str (p);
            msg ("%d: %s", rank, s);
            free (s);
        }
        subprocess_set_environ (p, environ);
        if (!optparse_hasopt (opts, "noexec")) {
            if (subprocess_run (p) < 0)
                err_exit ("subprocess_run");
        }
    }

    /* N.B. reap_all can be interrupted by SIGALRM, so keep calling
     * it until there are no more subprocesses o/w destroy will assert.
     */
    while (subprocess_manager_first (sm))
        subprocess_manager_reap_all (sm);

    subprocess_manager_destroy (sm);

    free (sid);
    free (sockdir);
    return (rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
