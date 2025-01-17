/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* runat.c - run named list of sequential commands
 *
 * Notes:
 * - Command env is inherited from broker, minus blocklist, plus FLUX_URI.
 * - All commands in a list are executed, even if one fails.
 * - The exit code of the first failed command is captured.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <jansson.h>
#if HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"
#include "ccan/str/str.h"

#include "runat.h"

struct runat_command {
    flux_subprocess_t *p;
    flux_cmd_t *cmd;
    int flags;
    struct timespec t_start;
};

struct runat_entry {
    char *name;
    zlist_t *commands;
    int exit_code;
    int count;
    bool aborted;
    bool completed;
    bool interactive;
    bool foreground;
    runat_completion_f cb;
    void *cb_arg;
};

struct runat {
    flux_t *h;
    const char *jobid;
    const char *local_uri;
    zhashx_t *entries;
    flux_msg_handler_t **handlers;
    bool sd_notify;
    struct termios saved_termios;
};

static void runat_command_destroy (struct runat_command *cmd);
static void start_next_command (struct runat *r, struct runat_entry *entry);

static const int abort_signal = SIGHUP;

static const char *env_blocklist[] = {
    "FLUX_JOB_ID",
    "FLUX_ENCLOSING_ID",
    "FLUX_JOB_SIZE",
    "FLUX_JOB_NNODES",
    "FLUX_JOB_TMPDIR",
    "FLUX_TASK_RANK",
    "FLUX_TASK_LOCAL_ID",
    "FLUX_URI",
    "FLUX_KVS_NAMESPACE",
    "FLUX_PROXY_REMOTE",
    "PMI_*",
    "FLUX_PMI_LIBRARY_PATH",
    "I_MPI_PMI_LIBRARY",
    "SLURM_*",  // flux-framework/flux-core#5206
    NULL,
};

extern char **environ;

static const char *get_shell (void)
{
    const char *shell = getenv ("SHELL");
    if (!shell)
        shell = "/bin/bash";
    return shell;
}

static char *get_cmdline (flux_cmd_t *cmd)
{
    char *buf = NULL;
    size_t len = 0;
    int i;
    int start = 0;

    /* Drop the "/bin/bash -c" from logging for brevity.
     */
    if (flux_cmd_argc (cmd) > 2
        && streq (flux_cmd_arg (cmd, 0), get_shell ())
        && streq (flux_cmd_arg (cmd, 1), "-c"))
        start += 2;
    for (i = start; i < flux_cmd_argc (cmd); i++) {
        if (argz_add (&buf, &len, flux_cmd_arg (cmd, i)) != 0) {
            free (buf);
            return NULL;
        }
    }
    argz_stringify (buf, len, ' ');
    return buf;
}

static void log_command (flux_t *h,
                         struct runat_entry *entry,
                         int rc,
                         double elapsed,
                         const char *s)
{
    struct runat_command *command = zlist_head (entry->commands);
    int command_index = entry->count - zlist_size (entry->commands);
    char *cmdline = get_cmdline (command->cmd);

    flux_log (h,
              rc == 0 ? LOG_INFO : LOG_ERR,
              "%s.%d: %s %s (rc=%d) %.1fs",
              entry->name,
              command_index,
              cmdline ? cmdline : "???",
              s,
              rc,
              elapsed);

    free (cmdline);
}

/* See POSIX 2008 Volume 3 Shell and Utilities, Issue 7
 * Section 2.8.2 Exit status for shell commands (page 2315)
 */
static void completion_cb (flux_subprocess_t *p)
{
    struct runat *r = flux_subprocess_aux_get (p, "runat");
    struct runat_entry *entry = flux_subprocess_aux_get (p, "runat_entry");
    struct runat_command *cmd = zlist_head (entry->commands);
    double elapsed = monotime_since (cmd->t_start) / 1000;
    int rc = flux_subprocess_exit_code (p);
    int signum;

    if (rc == 0 && entry->aborted) {
        rc = 1;
        log_command (r->h, entry, rc, elapsed, "aborted after exit with rc=0");
    }
    else if (rc >= 0)
        log_command (r->h, entry, rc, elapsed, "Exited");
    else if ((signum = flux_subprocess_signaled (p)) > 0) { // signaled
        rc = signum + 128;
        log_command (r->h, entry, rc, elapsed, strsignal (signum));
    }
    else { // ???
        rc = 1;
        log_command (r->h, entry, rc, elapsed, "???");
    }
    if (rc != 0 && entry->exit_code == 0) // capture first exit error
        entry->exit_code = rc;
    if (entry->foreground) {
        /*  This entry was moved to the foreground. Now that it has exited,
         *  restore the current process group to the foreground and
         *  reset terminal state.
         */
        if (tcsetpgrp (STDIN_FILENO, getpgrp ()) < 0
            || tcsetattr (STDIN_FILENO, TCSAFLUSH, &r->saved_termios) < 0)
        flux_log_error (r->h, "failed to reset foreground process group");
    }
    runat_command_destroy (zlist_pop (entry->commands));
    start_next_command (r, entry);
}

/* If state changes to running and the abort flag is set, send abort_signal.
 * This closes a race where the 'entry' might continue running if the abort
 * is called as a process is starting up.
 */
static void state_change_cb (flux_subprocess_t *p,
                             flux_subprocess_state_t state)
{
    struct runat *r = flux_subprocess_aux_get (p, "runat");
    struct runat_entry *entry = flux_subprocess_aux_get (p, "runat_entry");
    flux_future_t *f = NULL;

    switch (state) {
        case FLUX_SUBPROCESS_INIT:
        case FLUX_SUBPROCESS_EXITED:
        case FLUX_SUBPROCESS_FAILED:
            break;
        case FLUX_SUBPROCESS_STOPPED:
            /*
             *  If stdin is a tty and the broker is in a foreground process
             *  group, the subprocess may have stopped due to SIGTTIN/SIGTTOU.
             *  Attempt to bring the subprocess into the foreground and
             *  continue it. Set the foreground flag on the entry so that the
             *  broker knows to bring its own process group back into the
             *  foreground after this subprocess is complete.
             */
            if (isatty (STDIN_FILENO)
                && tcgetpgrp (STDIN_FILENO) == getpgrp ()) {
                entry->foreground = true;
                if (tcsetpgrp (STDIN_FILENO, flux_subprocess_pid (p)) < 0
                    || !(f = flux_subprocess_kill (p, SIGCONT))) {
                    flux_log_error (r->h,
                                    "error bringing %s into foreground",
                                    entry->name);
                }
                flux_future_destroy (f);
            }
            break;
        case FLUX_SUBPROCESS_RUNNING:
            if (entry->aborted) {
                if (!(f = flux_subprocess_kill (p, abort_signal))) {
                    if (errno != ESRCH)
                        flux_log_error (r->h, "kill %s", entry->name);
                }
                flux_future_destroy (f);
            }
            break;
    }
}

static void stdio_cb (flux_subprocess_t *p, const char *stream)
{
    struct runat *r = flux_subprocess_aux_get (p, "runat");
    struct runat_entry *entry = flux_subprocess_aux_get (p, "runat_entry");
    int index = entry->count - zlist_size (entry->commands);
    const char *line;
    int len;

    if ((len = flux_subprocess_getline (p, stream, &line)) > 0) {
        if (streq (stream, "stderr"))
            flux_log (r->h, LOG_ERR, "%s.%d: %s", entry->name, index, line);
        else
            flux_log (r->h, LOG_INFO, "%s.%d: %s", entry->name, index, line);
    }
}

/* Start one command.
 */
static flux_subprocess_t *start_command (struct runat *r,
                                         struct runat_entry *entry,
                                         struct runat_command *cmd)
{
    flux_subprocess_t *p;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_change_cb,
        .on_channel_out = NULL,
        .on_stdout = NULL,
        .on_stderr = NULL,
    };
    if (!(cmd->flags & FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH)) {
        ops.on_stdout = stdio_cb;
        ops.on_stderr = stdio_cb;
    }
    if (!(p = flux_local_exec_ex (flux_get_reactor (r->h),
                                  cmd->flags,
                                  cmd->cmd,
                                  &ops,
                                  NULL,
                                  flux_llog,
                                  r->h)))
        return NULL;
    if (flux_subprocess_aux_set (p, "runat_entry", entry, NULL) < 0)
        goto error;
    if (flux_subprocess_aux_set (p, "runat", r, NULL) < 0)
        goto error;
    monotime (&cmd->t_start);
    return p;
error:
    flux_subprocess_destroy (p);
    return NULL;
}

/* Start the next command.
 * If startup fails, try the next, and so on.
 */
static void start_next_command (struct runat *r, struct runat_entry *entry)
{
    struct runat_command *cmd;
    bool started = false;

    if (entry->aborted) {
        while ((cmd = zlist_pop (entry->commands)))
            runat_command_destroy (cmd);
    }
    else {
        while (!started && (cmd = zlist_head (entry->commands))) {
#if HAVE_LIBSYSTEMD
            if (r->sd_notify) {
                char *s = get_cmdline (cmd->cmd);
                sd_notifyf (0, "STATUS=Running %s", s ? s : "unknown command");
                free (s);
            }
#endif
            if (!(cmd->p = start_command (r, entry, cmd))) {
                log_command (r->h, entry, 1, 0, "error starting command");
                if (entry->exit_code == 0)
                    entry->exit_code = 1;
                runat_command_destroy (zlist_pop (entry->commands));
            }
            else
                started = true;
        }
    }
    if (zlist_size (entry->commands) == 0) {
        entry->completed = true;
        if (entry->cb)
            entry->cb (r, entry->name, entry->cb_arg);
    }
}

static void runat_command_destroy (struct runat_command *cmd)
{
    if (cmd) {
        int saved_errno = errno;
        flux_cmd_destroy (cmd->cmd);
        flux_subprocess_destroy (cmd->p);
        free (cmd);
        errno = saved_errno;
    }
}

static struct runat_command *runat_command_create (char **env, int flags)
{
    struct runat_command *cmd;

    if (!(cmd = calloc (1, sizeof (*cmd))))
        return NULL;
    if (!(flags & RUNAT_FLAG_LOG_STDIO))
        cmd->flags |= FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH;
    if (flags & RUNAT_FLAG_FORK_EXEC)
        cmd->flags |= FLUX_SUBPROCESS_FLAGS_FORK_EXEC;
    /*
     * N.B. By default subprocesses call setpgrp() before exec(2).  So
     * any processes spawned by command are also signaled by
     * flux_subprocess_signal()
     */
    if (!(cmd->cmd = flux_cmd_create (0, NULL, env)))
        goto error;
    return cmd;
error:
    runat_command_destroy (cmd);
    return NULL;
}

/* Unset blocklisted variables in command environment.
 * Set FLUX_ENCLOSING_ID if "jobid" is non-NULL.
 * Set FLUX_URI if local_uri is non-NULL.
 */
static int runat_command_modenv (struct runat_command *cmd,
                                 const char **blocklist,
                                 const char *local_uri,
                                 const char *jobid)
{
    if (blocklist) {
        int i;
        for (i = 0; blocklist[i] != NULL; i++)
            flux_cmd_unsetenv (cmd->cmd, blocklist[i]);
    }
    if (local_uri) {
        if (flux_cmd_setenvf (cmd->cmd, 1, "FLUX_URI", "%s", local_uri) < 0)
            return -1;
    }
    if (jobid) {
        if (flux_cmd_setenvf (cmd->cmd,
                              1,
                              "FLUX_ENCLOSING_ID",
                              "%s",
                              jobid) < 0)
            return -1;
    }
    return 0;
}

static int runat_command_set_argz (struct runat_command *cmd,
                                   const char *argz,
                                   size_t argz_len)
{
    char *arg = argz_next (argz, argz_len, NULL);
    while (arg) {
        if (flux_cmd_argv_append (cmd->cmd, arg) < 0)
            return -1;
        arg = argz_next (argz, argz_len, arg);
    }
    return 0;
}

static int runat_command_set_cmdline (struct runat_command *cmd,
                                      const char *shell,
                                      const char *cmdline)
{
    if (shell == NULL)
        shell = get_shell ();
    if (flux_cmd_argv_append (cmd->cmd, shell) < 0)
        return -1;
    if (cmdline) {
        if (flux_cmd_argv_append (cmd->cmd, "-c") < 0)
            return -1;
        if (flux_cmd_argv_append (cmd->cmd, cmdline) < 0)
            return -1;
    }
    return 0;
}

static void runat_entry_destroy (struct runat_entry *entry)
{
    if (entry) {
        int saved_errno = errno;
        if (entry->commands) {
            struct runat_command *cmd;
            while ((cmd = zlist_pop (entry->commands)))
                runat_command_destroy (cmd);
            zlist_destroy (&entry->commands);
        }
        free (entry->name);
        free (entry);
        errno = saved_errno;
    }
}

static struct runat_entry *runat_entry_create (const char *name)
{
    struct runat_entry *entry;

    if (!(entry = calloc (1, sizeof (*entry))))
        return NULL;
    if (!(entry->name = strdup (name)))
        goto error;
    if (!(entry->commands = zlist_new ()))
        goto error;
    return entry;
error:
    runat_entry_destroy (entry);
    return NULL;
}

/* zhashx_destructor_fn signature */
static void runat_entry_destroy_wrapper (void **arg)
{
    if (arg) {
        runat_entry_destroy (*arg);
        *arg = NULL;
    }
}

/* Push 'cmd' onto command list 'name', creating it if it doesn't exist.
 */
static int runat_push (struct runat *r,
                       const char *name,
                       struct runat_command *cmd,
                       bool interactive)
{
    struct runat_entry *entry;

    if (!(entry = zhashx_lookup (r->entries, name))) {
        if (!(entry = runat_entry_create (name)))
            return -1;
        (void)zhashx_insert (r->entries, name, entry);
    }
    if (zlist_push (entry->commands, cmd) < 0) {
        if (zlist_size (entry->commands) == 0)
            zhashx_delete (r->entries, name);
        errno = ENOMEM;
        return -1;
    }
    entry->count++;
    if (!entry->interactive)
        entry->interactive = interactive;
    return 0;
}

int runat_push_shell_command (struct runat *r,
                              const char *name,
                              const char *cmdline,
                              int flags)
{
    struct runat_command *cmd;

    if (!r || !name || !cmdline) {
        errno = EINVAL;
        return -1;
    }
    if (!(cmd = runat_command_create (environ, flags)))
        return -1;
    if (runat_command_set_cmdline (cmd, NULL, cmdline) < 0)
        goto error;
    if (runat_command_modenv (cmd, env_blocklist, r->local_uri, r->jobid) < 0)
        goto error;
    if (runat_push (r, name, cmd, false) < 0)
        goto error;
    return 0;
error:
    runat_command_destroy (cmd);
    return -1;
}

int runat_push_shell (struct runat *r,
                      const char *name,
                      const char *shell,
                      int flags)
{
    struct runat_command *cmd;

    if (!r || !name || (flags & RUNAT_FLAG_LOG_STDIO)) {
        errno = EINVAL;
        return -1;
    }
    if (!(cmd = runat_command_create (environ, flags)))
        return -1;
    if (runat_command_set_cmdline (cmd, shell, NULL) < 0)
        goto error;
    if (runat_command_modenv (cmd, env_blocklist, r->local_uri, r->jobid) < 0)
        goto error;
    if (runat_push (r, name, cmd, true) < 0)
        goto error;
    return 0;
error:
    runat_command_destroy (cmd);
    return -1;
}

int runat_push_command (struct runat *r,
                        const char *name,
                        const char *argz,
                        size_t argz_len,
                        int flags)
{
    struct runat_command *cmd;

    if (!r || !name || !argz) {
        errno = EINVAL;
        return -1;
    }
    if (!(cmd = runat_command_create (environ, flags)))
        return -1;
    if (runat_command_set_argz (cmd, argz, argz_len) < 0)
        goto error;
    if (runat_command_modenv (cmd, env_blocklist, r->local_uri, r->jobid) < 0)
        goto error;
    if (runat_push (r, name, cmd, false) < 0)
        goto error;
    return 0;
error:
    runat_command_destroy (cmd);
    return -1;
}

int runat_get_exit_code (struct runat *r, const char *name, int *rc)
{
    struct runat_entry *entry;

    if (!r || !name || !rc) {
        errno = EINVAL;
        return -1;
    }
    if (!(entry = zhashx_lookup (r->entries, name))) {
        errno = ENOENT;
        return -1;
    }
    *rc = entry->exit_code;
    return 0;
}

int runat_start (struct runat *r,
                 const char *name,
                 runat_completion_f cb,
                 void *arg)
{
    struct runat_entry *entry;

    if (!r || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(entry = zhashx_lookup (r->entries, name))) {
        errno = ENOENT;
        return -1;
    }
    entry->cb = cb;
    entry->cb_arg = arg;
    start_next_command (r, entry);
    return 0;
}

bool runat_is_defined (struct runat *r, const char *name)
{
    if (!r || !name || !zhashx_lookup (r->entries, name))
        return false;
    return true;
}

bool runat_is_completed (struct runat *r, const char *name)
{
    struct runat_entry *entry;

    if (!r || !name || !(entry = zhashx_lookup (r->entries, name)))
        return false;
    return entry->completed;
}

bool runat_is_interactive (struct runat *r, const char *name)
{
    struct runat_entry *entry;

    if (!r || !name || !(entry = zhashx_lookup (r->entries, name)))
        return false;
    return entry->interactive;
}

int runat_abort (struct runat *r, const char *name)
{
    struct runat_entry *entry;
    struct runat_command *cmd;

    if (!r || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(entry = zhashx_lookup (r->entries, name))) {
        errno = ENOENT;
        return -1;
    }
    if ((cmd = zlist_head (entry->commands)) && cmd->p != NULL) {
        flux_future_t *f;
        if (!(f = flux_subprocess_kill (cmd->p, abort_signal))) {
            if (errno != ESRCH)
                flux_log_error (r->h, "kill %s", entry->name);
        }
        flux_future_destroy (f);
    }
    entry->aborted = true;
    return 0;
}

static void runat_push_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct runat *r = arg;
    const char *name;
    json_t *commands;
    const char *errstr = NULL;
    size_t index;
    json_t *el;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:o}",
                             "name",
                             &name,
                             "commands",
                             &commands) < 0)
        goto error;
    if (json_array_size (commands) == 0) {
        errno = EPROTO;
        errstr = "commands array is empty";
        goto error;
    }
    json_array_foreach (commands, index, el) {
        const char *cmdline = json_string_value (el);
        if (!cmdline || strlen (cmdline) == 0) {
            errno = EPROTO;
            errstr = "cannot push an empty command line";
            goto error;
        }
        if (runat_push_shell_command (r,
                                      name,
                                      cmdline,
                                      RUNAT_FLAG_LOG_STDIO) < 0)
            goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log (h, LOG_ERR, "error responding to runat.push");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log (h, LOG_ERR, "error responding to runat.push");
}


static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "runat.push", runat_push_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct runat *runat_create (flux_t *h,
                            const char *local_uri,
                            const char *jobid,
                            bool sdnotify)
{
    struct runat *r;

    if (!(r = calloc (1, sizeof (*r))))
        return NULL;
    if (!(r->entries = zhashx_new ()))
        goto error;
    if (flux_msg_handler_addvec (h, htab, r, &r->handlers) < 0)
        goto error;
    zhashx_set_destructor (r->entries, runat_entry_destroy_wrapper);
    r->h = h;
    r->jobid = jobid;
    r->local_uri = local_uri;
    r->sd_notify = sdnotify;
    if (isatty (STDIN_FILENO)
        && tcgetattr (STDIN_FILENO, &r->saved_termios) < 0)
        flux_log_error (r->h, "failed to save terminal attributes");
    return r;
error:
    runat_destroy (r);
    return NULL;
}

void runat_destroy (struct runat *r)
{
    if (r) {
        int saved_errno = errno;
        zhashx_destroy (&r->entries);
        flux_msg_handler_delvec (r->handlers);
        free (r);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
