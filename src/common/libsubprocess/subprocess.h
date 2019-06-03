/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_SUBPROCESS_H
#define _FLUX_CORE_SUBPROCESS_H

#include <sys/types.h>

#include <flux/core.h>

/*
 *  flux_cmd_t: An object that defines a command to be run, either
 *   remotely or as a child of the current process. Includes cmdline
 *   arguments, environment, and working directory. A flux_cmd_t is
 *   used to create Flux subprocesses.
 */
typedef struct flux_command flux_cmd_t;

/*
 *  flux_subprocess_t: A subprocess is an instantiation of a command
 *   as a remote or local process. A subprocess has a state (e.g.
 *   initialized, starting, running, exited, completed), a PID, and
 *   a rank if running remotely.
 */
typedef struct flux_subprocess flux_subprocess_t;

/*  flux_subprocess_server_t: Handler for a subprocess remote server */
typedef struct flux_subprocess_server flux_subprocess_server_t;

/*
 * Subprocess states, on changes, will lead to calls to
 * on_state_change below.
 *
 * Possible state changes:
 *
 * init -> running
 * init -> exec failed
 * running -> exited
 * any state -> failed
 */
typedef enum {
    FLUX_SUBPROCESS_INIT,         /* initial state */
    FLUX_SUBPROCESS_EXEC_FAILED,  /* exec(2) has failed, only for rexec() */
    FLUX_SUBPROCESS_RUNNING,      /* exec(2) has been called */
    FLUX_SUBPROCESS_EXITED,       /* process has exited */
    FLUX_SUBPROCESS_FAILED,       /* internal failure, catch all for
                                   * all other errors */
} flux_subprocess_state_t;

/*
 * Subprocess flags
 */
enum {
    /* flux_exec(): let parent stdin, stdout, stderr, carry to child.
     * Do not create "STDIN", "STDOUT", or "STDERR" channels.  Subsequently,
     * flux_subprocess_write()/close()/read()/read_line() will fail on
     * streams of "STDIN", "STDOUT", or "STDERR".
     */
    FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH = 1,
    /* flux_exec(): call setpgrp() before exec(2) */
    FLUX_SUBPROCESS_FLAGS_SETPGRP = 2,
};

/*
 *  Typedefs for subprocess hooks and callbacks:
 *
 */
typedef void (*flux_subprocess_f) (flux_subprocess_t *p);
typedef void (*flux_subprocess_output_f) (flux_subprocess_t *p,
                                          const char *stream);
typedef void (*flux_subprocess_state_f) (flux_subprocess_t *p,
                                         flux_subprocess_state_t state);
typedef void (*flux_subprocess_hook_f) (flux_subprocess_t *p, void *arg);

/*
 *  Functions for event-driven subprocess handling:
 *
 */
typedef struct {
    flux_subprocess_f on_completion;    /* Process exited and all I/O
                                         * complete, will not be
                                         * called if EXEC_FAILED or
                                         * FAILED states reached.
                                         */
    flux_subprocess_state_f on_state_change;  /* Process state change        */
    flux_subprocess_output_f on_channel_out; /* Read from channel when ready */
    flux_subprocess_output_f on_stdout; /* Read of stdout is ready           */
    flux_subprocess_output_f on_stderr; /* Read of stderr is ready           */
} flux_subprocess_ops_t;

/*
 *  flux_subprocess_hooks_t: Hook functions to execute at pre-defined
 *  points.  Hooks can only be executed on local processes.
 */
typedef struct {
    flux_subprocess_hook_f pre_exec;
    void *pre_exec_arg;
    flux_subprocess_hook_f post_fork;
    void *post_fork_arg;
} flux_subprocess_hooks_t;

/*
 *  General support:
 */

/*  Start a subprocess server on the handle `h`. Registers message
 *   handlers, etc for remote execution. "prefix" is the topic prefix
 *   used to listen for this service, e.g. `broker` would listen
 *   for `broker.exec`.
 */
flux_subprocess_server_t *flux_subprocess_server_start (flux_t *h,
                                                        const char *prefix,
                                                        const char *local_uri,
                                                        uint32_t rank);

/*  Stop a subprocess server / cleanup flux_subprocess_server_t.  Will
 *  send a SIGKILL to all remaining subprocesses.
 */
void flux_subprocess_server_stop (flux_subprocess_server_t *s);

/* Send all subprocesses signal and wait up to wait_time seconds for
 * all subprocesses to complete.  This is typically called to send
 * SIGTERM before calling flux_subprocess_server_stop(), allowing
 * users to send a signal to inform subprocesses to complete / cleanup
 * before they are sent SIGKILL.
 *
 * This function will enter the reactor to wait for subprocesses to
 * complete, should only be called on cleanup path when primary
 * reactor has exited.
 */
int flux_subprocess_server_subprocesses_kill (flux_subprocess_server_t *s,
                                              int signum,
                                              double wait_time);

/* Terminate all subprocesses started by a sender id */
int flux_subprocess_server_terminate_by_uuid (flux_subprocess_server_t *s,
                                              const char *id);

/*
 * Convenience Functions:
 */

/*  General output callback that will send output from the subprocess
 *  to stdout or stderr.  Set the `on_stdout` and/or `on_stderr`
 *  callbacks in flux_subprocess_ops_t and this function will output
 *  to stdout/stderr respectively.  You can also set 'on_channel_out'
 *  to this function, which will send all channel output to stdout.
 */
void flux_standard_output (flux_subprocess_t *p, const char *stream);

/*
 *  Commands:
 */

/*
 *  Create a cmd object, from which subprocesses can be created
 */
flux_cmd_t * flux_cmd_create (int argc, char *argv[], char **env);

/*
 *  Create a copy of a cmd object.
 */
flux_cmd_t * flux_cmd_copy (const flux_cmd_t *cmd);

/*
 *  Destroy and free command object `cmd`
 */
void flux_cmd_destroy (flux_cmd_t *cmd);

/*
 *  Append formatted string to argv of `cmd`.
 */
int flux_cmd_argv_append (flux_cmd_t *cmd, const char *fmt, ...);

/*
 *  Return the current argument count for `cmd`.
 */
int flux_cmd_argc (const flux_cmd_t *cmd);

/*
 *  Return the current argument at index n (range 0 to argc - 1)
 */
const char *flux_cmd_arg (const flux_cmd_t *cmd, int n);

/*
 *  Set a single environment variable (name) to formatted string `fmt`.
 *   If `overwrite` is non-zero then overwrite any existing setting for `name`.
 */
int flux_cmd_setenvf (flux_cmd_t *cmd, int overwrite,
		      const char *name, const char *fmt, ...);

/*
 *  Unset environment variable `name` in the command object `cmd`.
 */
void flux_cmd_unsetenv (flux_cmd_t *cmd, const char *name);

/*
 *  Return current value for environment variable `name` as set in
 *   command object `cmd`. If environment variable is not set then NULL
 *   is returned.
 */
const char *flux_cmd_getenv (const flux_cmd_t *cmd, const char *name);

/*
 *  Set/get the working directory for the command `cmd`.
 */
int flux_cmd_setcwd (flux_cmd_t *cmd, const char *cwd);
const char *flux_cmd_getcwd (const flux_cmd_t *cmd);

/*
 *  Request a channel for communication between process and caller.
 *   Callers can write to the subproces via flux_subprocess_write()
 *   and read from it via flux_subprocess_read(), which is typically
 *   called from a callback set in 'on_channel_out'.
 *
 *  The `name` argument is also used as the name of the environment variable
 *   in the subprocess environment that is set to the file descriptor number
 *   of the process side of the socketpair. E.g. name = "FLUX_PMI_FD" would
 *   result in the environment variable "FLUX_PMI_FD=N" set in the process
 *   environment.
 */
int flux_cmd_add_channel (flux_cmd_t *cmd, const char *name);

/*
 *  Set generic string options for command object `cmd`. As with environment
 *   variables, this function adds the option `var` to with value `val` to
 *   the options array for this command. This can be used to enable optional
 *   behavior for executed processes (e.g. setpgrp(2))
 *
 *  String options, note that name indicates the 'name' argument used
 *  in flux_cmd_add_channel() above.
 *
 *  name + "_BUFSIZE" = buffer size
 *  STDIN_BUFSIZE = buffer size
 *  STDOUT_BUFSIZE = buffer size
 *  STDERR_BUFSIZE = buffer size
 *
 *  By default, stdio and channels use an internal buffer of 1 meg.
 *  The buffer size can be adjusted with this option.
 */
int flux_cmd_setopt (flux_cmd_t *cmd, const char *var, const char *val);
const char *flux_cmd_getopt (flux_cmd_t *cmd, const char *var);

/*
 *  Subprocesses:
 */

/*
 *  Asynchronously create a new subprocess described by command object
 *   `cmd`.  flux_exec() and flux_local_exec() create a new subprocess
 *   locally.  flux_rexec() creates a new subprocess on Flux rank
 *   `rank`. Callbacks in `ops` structure that are non-NULL will be
 *   called to process state changes, I/O, and completion.
 *
 *  'rank' can be set to FLUX_NODEID_ANY or FLUX_NODEID_UPSTREAM.
 *
 *  This function may return NULL (with errno set) on invalid
 *   argument(s) (EINVAL), or failure of underlying Flux messaging
 *   calls. Otherwise, a valid subprocess object is returned, though
 *   there is no guarantee the subprocess has started running anywhere
 *   by the time the call returns.
 *
 */
flux_subprocess_t *flux_exec (flux_t *h, int flags,
                              const flux_cmd_t *cmd,
                              const flux_subprocess_ops_t *ops,
                              const flux_subprocess_hooks_t *hooks);

flux_subprocess_t *flux_local_exec (flux_reactor_t *r, int flags,
                                    const flux_cmd_t *cmd,
                                    const flux_subprocess_ops_t *ops,
                                    const flux_subprocess_hooks_t *hooks);

flux_subprocess_t *flux_rexec (flux_t *h, int rank, int flags,
                               const flux_cmd_t *cmd,
                               const flux_subprocess_ops_t *ops);


/*
 *  Write data to "stream" stream of subprocess `p`.  'stream' can be
 *  "STDIN" or the name of a stream specified with
 *  flux_cmd_add_channel().  If 'stream' is NULL, defaults to "STDIN".
 *
 *  Returns the total amount of data successfully buffered.
 */
int flux_subprocess_write (flux_subprocess_t *p, const char *stream,
                           const char *buf, size_t len);

/*
 *  Close "stream" stream of subprocess `p` and schedule EOF to be sent.
 *  'stream' can be "STDIN" or the name of a stream specified with
 *  flux_cmd_add_channel().  If 'stream' is NULL, defaults to "STDIN".
 */
int flux_subprocess_close (flux_subprocess_t *p, const char *stream);

/*
 *  Read up to `len` bytes of unread data from stream `stream`.  To
 *   read all data, specify 'len' of -1.  'stream' can be "STDOUT",
 *   "STDERR", or the name of a stream specified with
 *   flux_cmd_add_channel().  If 'stream' is NULL, defaults to
 *   "STDOUT".
 *
 *   Returns pointer to buffer on success and NULL on error with errno
 *   set.  If reading from "STDOUT" or "STDERR", buffer is guaranteed
 *   to be NUL terminated.  User shall not free returned pointer.
 *   Length of buffer returned can optionally returned in 'lenp'.  A
 *   length of 0 indicates that the subprocess has closed this stream.
 */
const char *flux_subprocess_read (flux_subprocess_t *p,
                                  const char *stream,
                                  int len, int *lenp);

/*
 *  Read line unread data from stream `stream`.  'stream' can be
 *   "STDOUT", "STDERR", or the name of a stream specified with
 *   flux_cmd_add_channel().  If 'stream' is NULL, defaults to
 *   "STDOUT".
 *
 *   Returns pointer to buffer on success and NULL on error with errno
 *   set.  If reading from "STDOUT" or "STDERR", buffer is guaranteed
 *   to be NUL terminated.  User shall not free returned pointer.
 *   Length of buffer returned can optionally returned in 'lenp'.
 */
const char *flux_subprocess_read_line (flux_subprocess_t *p,
                                       const char *stream,
                                       int *lenp);

/* Identical to flux_subprocess_read_line(), but does not return
 * trailing newline.
 */
const char *flux_subprocess_read_trimmed_line (flux_subprocess_t *p,
                                               const char *stream,
                                               int *lenp);

/*
 *  Create RPC to send signal `signo` to subprocess `p`.
 *  This call returns a flux_future_t. Use flux_future_then(3) to register
 *   a continuation callback when the kill operation is complete, or
 *   flux_future_wait_for(3) to block until the kill operation is complete.
 */
flux_future_t *flux_subprocess_kill (flux_subprocess_t *p, int signo);

/*
 *  Add/remove a reference to subprocess object `p`. The subprocess object
 *   is destroyed once the last reference is removed.
 */
void flux_subprocess_ref (flux_subprocess_t *p);
void flux_subprocess_unref (flux_subprocess_t *p);
#define flux_subprocess_destroy(x) flux_subprocess_unref(x)

/*  Return current state value of subprocess.  Note this may differ
 *  than state returned in on_state_change callback, as a subprocess
 *  may have already transitioned past that point (e.g. the callback
 *  received a transition change to RUNNING, but the child subprocess
 *  has already EXITED).
 */
flux_subprocess_state_t flux_subprocess_state (flux_subprocess_t *p);

/*  Return string value of state of subprocess
 */
const char *flux_subprocess_state_string (flux_subprocess_state_t state);

int flux_subprocess_rank (flux_subprocess_t *p);

/* Returns the errno causing the FLUX_SUBPROCESS_EXEC_FAILED or
 * FLUX_SUBPROCESS_FAILED states to be reached.
 */
int flux_subprocess_fail_errno (flux_subprocess_t *p);

/* Returns exit status as returned from wait(2).  Works only for
 * FLUX_SUBPROCESS_EXITED state. */
int flux_subprocess_status (flux_subprocess_t *p);

/* Returns exit code if processes exited normally.  Works only for
 * FLUX_SUBPROCESS_EXITED state. */
int flux_subprocess_exit_code (flux_subprocess_t *p);

/* Returns signal if process terminated via signal.  Works only for
 * FLUX_SUBPROCESS_EXITED state. */
int flux_subprocess_signaled (flux_subprocess_t *p);

pid_t flux_subprocess_pid (flux_subprocess_t *p);

/*  Return the command object associated with subprocess `p`.
 */
flux_cmd_t *flux_subprocess_get_cmd (flux_subprocess_t *p);

/*  Return the reactor object associated with subprocess `p`.
 */
flux_reactor_t * flux_subprocess_get_reactor (flux_subprocess_t *p);

/*
 *  Set arbitrary context `ctx` with name `name` on subprocess object `p`.
 *
 *  Returns 0 on success
 */
int flux_subprocess_aux_set (flux_subprocess_t *p,
                             const char *name, void *ctx, flux_free_f free);

/*
 *  Return pointer to any context associated with `p` under `name`. If
 *   no such context exists, then NULL is returned.
 */
void *flux_subprocess_aux_get (flux_subprocess_t *p, const char *name);

#endif /* !_FLUX_CORE_SUBPROCESS_H */
