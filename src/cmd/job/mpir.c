/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job MPIR support */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libdebugged/debugged.h"
#include "src/shell/mpir/proctable.h"
#include "ccan/str/str.h"

extern char **environ;

#ifndef VOLATILE
# if defined(__STDC__) || defined(__cplusplus)
# define VOLATILE volatile
# else
# define VOLATILE
# endif
#endif

#define MPIR_NULL                  0
#define MPIR_DEBUG_SPAWNED         1
#define MPIR_DEBUG_ABORTING        2

VOLATILE int MPIR_debug_state    = MPIR_NULL;
struct proctable *proctable      = NULL;
MPIR_PROCDESC *MPIR_proctable    = NULL;
int MPIR_proctable_size          = 0;
char *MPIR_debug_abort_string    = NULL;
int MPIR_i_am_starter            = 1;
int MPIR_acquired_pre_main       = 1;
int MPIR_force_to_main           = 1;
int MPIR_partial_attach_ok       = 1;

char MPIR_executable_path[256];
char MPIR_server_arguments[1024];

static void setup_mpir_proctable (const char *s)
{
    if (!(proctable = proctable_from_json_string (s))) {
        errno = EINVAL;
        log_err_exit ("proctable_from_json_string");
    }
    MPIR_proctable = proctable_get_mpir_proctable (proctable,
                                                   &MPIR_proctable_size);
    if (!MPIR_proctable) {
        errno = EINVAL;
        log_err_exit ("proctable_get_mpir_proctable");
    }
}

static void gen_attach_signal (flux_t *h, flux_jobid_t id)
{
    flux_future_t *f = NULL;
    if (!(f = flux_job_kill (h, id, SIGCONT)))
        log_err_exit ("flux_job_kill");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("kill %s: %s",
                      idf58 (id),
                      future_strerror (f, errno));
    flux_future_destroy (f);
}

static int mpir_args_assign (const char *argv0,
                             const char *args,
                             int args_len,
                             int *argc,
                             const char ***argv)
{
    int i = 0;
    int n = 0;

    *argv = NULL;
    *argc = 0;

    /*  First get number of entries in args */
    while (i < args_len) {
        int len = strlen (args+i);
        if (len == 0)
            break;
        n++;
        i += len + 1;
    }
    n = n + 1;

    if (!(*argv = calloc (n+1, sizeof (char *)))) {
        log_err ("MPIR: failed to allocate argv for tool launch");
        return -1;
    }

    (*argv)[0] = argv0;
    *argc = n;

    if (n == 1)
        return 0;

    /*  Now assign remainder of argv */
    i = 0;
    n = 1;
    while (i < args_len) {
        int len = strlen (args+i);
        if (len == 0)
            break;
        (*argv)[n++] = args+i;
        i += len + 1;
    }
    return 0;
}

static int mpir_create_argv (const char path[],
                             const char args[],
                             int args_len,
                             int *argc,
                             const char ***argv)
{
    return mpir_args_assign (path, args, args_len, argc, argv);
}

static void completion_cb (flux_subprocess_t *p)
{
    int rank = flux_subprocess_rank (p);
    int exitcode = flux_subprocess_exit_code (p);
    int signum = flux_subprocess_signaled (p);
    const char *prog = basename (MPIR_executable_path);

    if (signum > 0)
        log_msg ("MPIR: rank %d: %s: %s", rank, prog, strsignal (signum));
    else if (exitcode != 0)
        log_msg ("MPIR: rank %d: %s: Exit %d", rank, prog, exitcode);
    flux_subprocess_destroy (p);
}

static flux_cmd_t *mpir_make_tool_cmd (const char *path,
                                       const char *server_args,
                                       int server_args_len)
{
    flux_cmd_t *cmd = NULL;
    char *dir = NULL;
    char **argv;
    int argc;

    if (mpir_create_argv (path,
                          server_args,
                          server_args_len,
                          &argc,
                          (const char ***) &argv) < 0)
        return NULL;

    if (argc == 0
        || !(cmd = flux_cmd_create (argc, argv, environ))) {
        log_err ("failed to create command from MPIR_executable_path");
        return NULL;
    }

    flux_cmd_unsetenv (cmd, "FLUX_PROXY_REMOTE");
    if (!(dir = get_current_dir_name ())
        || flux_cmd_setcwd (cmd, dir) < 0)
        log_err_exit ("failed to get or set current directory");
    free (argv);
    free (dir);
    return cmd;
}

static void output_cb (flux_subprocess_t *p, const char *stream)
{
    const char *line;
    int len = 0;
    const char *prog = basename (MPIR_executable_path);
    int rank = flux_subprocess_rank (p);

    len = flux_subprocess_read_trimmed_line (p, stream, &line);
    if (len == 0)
        len = flux_subprocess_read (p, stream, &line);
    if (len > 0)
        log_msg ("MPIR: rank %d: %s: %s: %s", rank, prog, stream, line);
}

static void state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    if (state == FLUX_SUBPROCESS_FAILED) {
        const char *prog = basename (MPIR_executable_path);
        int rank = flux_subprocess_rank (p);
        const char *errmsg = flux_subprocess_fail_error (p);
        log_msg ("MPIR: rank %d: %s: %s", rank, prog, errmsg);
        flux_subprocess_destroy (p);
    }
}

static void launch_tool_daemons (flux_t *h,
                                 const char *exec_service,
                                 const char *tool_path,
                                 const char *tool_args,
                                 int tool_args_len,
                                 struct idset *ranks)
{
    unsigned int rank;
    flux_cmd_t *cmd;

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = output_cb,
        .on_stderr = output_cb,
        .on_state_change = state_cb,
    };

    if (!(cmd = mpir_make_tool_cmd (tool_path, tool_args, tool_args_len)))
        return;

    rank = idset_first (ranks);
    while (rank != IDSET_INVALID_ID) {
        flux_subprocess_t *p;
        if (!(p = flux_rexec_ex (h,
                                 exec_service,
                                 rank,
                                 0,
                                 cmd,
                                 &ops,
                                 NULL,
                                 NULL)))
            log_err ("MPIR: failed to launch %s", tool_path);
        rank = idset_next (ranks, rank);
    }
    flux_cmd_destroy (cmd);
    return;
}

void mpir_setup_interface (flux_t *h,
                           flux_jobid_t id,
                           bool debug_emulate,
                           bool stop_tasks_in_exec,
                           int leader_rank,
                           const char *shell_service)
{
    int len;
    char topic [1024];
    const char *s = NULL;
    flux_future_t *f = NULL;

    len = snprintf (topic, sizeof (topic), "%s.proctable", shell_service);
    if (len >= sizeof (topic))
        log_msg_exit ("mpir: failed to create shell proctable topic string");

    if (!(f = flux_rpc_pack (h, topic, leader_rank, 0, "{}")))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get (f, &s) < 0)
        log_err_exit ("%s", topic);

    setup_mpir_proctable (s);
    flux_future_destroy (f);

    if (strlen (MPIR_executable_path) > 0) {
        struct idset *ranks = proctable_get_ranks (proctable, NULL);
        len = snprintf (topic, sizeof (topic), "%s.rexec", shell_service);
        if (len >= sizeof (topic))
            log_msg_exit ("mpir: failed to create shell rexec topic string");
        launch_tool_daemons (h,
                             topic,
                             MPIR_executable_path,
                             MPIR_server_arguments,
                             sizeof (MPIR_server_arguments),
                             ranks);
        idset_destroy (ranks);
    }

    MPIR_debug_state = MPIR_DEBUG_SPAWNED;

    /* Signal the parallel debugger */
    MPIR_Breakpoint ();

    if (stop_tasks_in_exec || debug_emulate) {
        /* To support MPIR_partial_attach_ok, we need to send SIGCONT to
         * those MPI processes to which the debugger didn't attach.
         * However, all of the debuggers that I know of do ignore
         * additional SIGCONT being sent to the processes they attached to.
         * Therefore, we send SIGCONT to *every* MPI process.
         *
         * We also send SIGCONT under the debug-emulate flag. This allows us
         * to write a test for attach mode. The running job will exit
         * on SIGCONT.
         */
        gen_attach_signal (h, id);
    }
}

/* vi: ts=4 sw=4 expandtab
 */
