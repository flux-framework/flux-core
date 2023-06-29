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

#ifdef __cplusplus
extern "C" {
#endif

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

/*
 * Subprocess states, on changes, will lead to calls to
 * on_state_change below.
 *
 * Possible state changes:
 *
 * init -> running
 * running -> exited
 * any state -> failed
 */
typedef enum {
    FLUX_SUBPROCESS_INIT,         /* initial state */
    FLUX_SUBPROCESS_RUNNING,      /* exec(2) has been called */
    FLUX_SUBPROCESS_EXITED,       /* process has exited */
    FLUX_SUBPROCESS_FAILED,       /* exec failure or other non-child error */
    FLUX_SUBPROCESS_STOPPED,      /* process was stopped */
} flux_subprocess_state_t;

/*
 * Subprocess flags
 */
enum {
    /* flux_exec(): let parent stdin, stdout, stderr, carry to child.
     * Do not create "stdin", "stdout", or "stderr" channels.  Subsequently,
     * flux_subprocess_write()/close()/read()/read_line() will fail on
     * streams of "stdin", "stdout", or "stderr".
     */
    FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH = 1,
    /* flux_exec(): call setpgrp() before exec(2) */
    FLUX_SUBPROCESS_FLAGS_SETPGRP = 2,
    /* use fork(2)/exec(2) even if posix_spawn(3) available */
    FLUX_SUBPROCESS_FLAGS_FORK_EXEC = 4,
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
                                         * called if FAILED state reached.
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
 *  llog-compatible callback
 */
typedef void (*subprocess_log_f) (void *arg,
                                  const char *file,
                                  int line,
                                  const char *func,
                                  const char *subsys,
                                  int level,
                                  const char *fmt,
                                  va_list args);


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
int flux_cmd_argv_appendf (flux_cmd_t *cmd, const char *fmt, ...)
                           __attribute__ ((format (printf, 2, 3)));

/*
 *  Append string to argv of `cmd`.
 */
int flux_cmd_argv_append (flux_cmd_t *cmd, const char *arg);

/*
 *  Delete the nth argument in cmd's argv
 */
int flux_cmd_argv_delete (flux_cmd_t *cmd, int n);

/*
 *  Insert arg before the nth argument in cmd's argv
 */
int flux_cmd_argv_insert (flux_cmd_t *cmd, int n, const char *arg);

/*
 *  Return the current argument count for `cmd`.
 */
int flux_cmd_argc (const flux_cmd_t *cmd);

/*
 *  Return the current argument at index n (range 0 to argc - 1)
 */
const char *flux_cmd_arg (const flux_cmd_t *cmd, int n);

/*
 *  Return a copy of the current cmd as a string. Caller must free
 */
char *flux_cmd_stringify (const flux_cmd_t *cmd);

/*
 *  Set a single environment variable (name) to formatted string `fmt`.
 *   If `overwrite` is non-zero then overwrite any existing setting for `name`.
 */
int flux_cmd_setenvf (flux_cmd_t *cmd, int overwrite,
                      const char *name, const char *fmt, ...)
                      __attribute__ ((format (printf, 4, 5)));

/*
 *  Unset environment variable `name` in the command object `cmd`.
 *   If `name` is a glob pattern, unset all matching variables.
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
 *   variables, this function adds the option `var` with value `val` to
 *   the options array for this command. This can be used to enable optional
 *   behavior for executed processes (e.g. setpgrp(2))
 *
 *  String options, note that name indicates the 'name' argument used
 *  in flux_cmd_add_channel() above.
 *
 *  "BUFSIZE" option
 *
 *   By default, stdio and channels use an internal buffer of 4 megs.
 *   The buffer size can be adjusted with this option.
 *
 *   - name + "_BUFSIZE" - set buffer size on channel name
 *   - stdin_BUFSIZE - set buffer size on stdin
 *   - stdout_BUFSIZE - set buffer size on stdout
 *   - stderr_BUFSIZE - set buffer size on stderr
 *
 *  "LINE_BUFFER" option
 *
 *    By default, output callbacks such as 'on_stdout' and 'on_stderr'
 *    are called when a line of data is available (with the exception
 *    with data after a subprocess has exited).  By setting this
 *    option to "false", output callbacks will be called whenever any
 *    amount of data is available.  These options can also be set to
 *    "true" to keep default behavior of line buffering.
 *
 *    - name + "_LINE_BUFFER" - configuring line buffering on channel name
 *    - stdout_LINE_BUFFER - configure line buffering for stdout
 *    - stderr_LINE_BUFFER - configure line buffering for stderr
 *
 *  "STREAM_STOP" option
 *
 *    By default, the output callbacks such as 'on_stdout' and
 *    'on_stderr' can immediately begin receiving stdout/stderr data
 *    once a subprocess has started.  There are circumstances where a
 *    caller may wish to wait and can have these callbacks stopped by
 *    default and restarted later by flux_subprocess_stream_start().
 *    By setting this option to "true", output callbacks will be
 *    stopped by default.  These options can also be set to "false" to
 *    keep default behavior.  Note that these options only apply to
 *    local subprocesses.
 *
 *    - name + "_STREAM_STOP" - configure start/stop on channel name
 *    - stdout_STREAM_STOP - configure start/stop for stdout
 *    - stderr_STREAM_STOP - configure start/stop for stderr
 */
int flux_cmd_setopt (flux_cmd_t *cmd, const char *var, const char *val);
const char *flux_cmd_getopt (flux_cmd_t *cmd, const char *var);

/*
 *  Subprocesses:
 */

/*
 *  Asynchronously create a new subprocess described by command object
 *   `cmd`.  flux_local_exec() create a new subprocess locally.
 *   flux_rexec() creates a new subprocess on Flux rank
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
flux_subprocess_t *flux_local_exec (flux_reactor_t *r,
                                    int flags,
                                    const flux_cmd_t *cmd,
                                    const flux_subprocess_ops_t *ops);

flux_subprocess_t *flux_local_exec_ex (flux_reactor_t *r,
                                       int flags,
                                       const flux_cmd_t *cmd,
                                       const flux_subprocess_ops_t *ops,
                                       const flux_subprocess_hooks_t *hooks,
                                       subprocess_log_f log_fn,
                                       void *log_data);

flux_subprocess_t *flux_rexec (flux_t *h, int rank, int flags,
                               const flux_cmd_t *cmd,
                               const flux_subprocess_ops_t *ops);

flux_subprocess_t *flux_rexec_ex (flux_t *h,
                                  const char *service_name,
                                  int rank,
                                  int flags,
                                  const flux_cmd_t *cmd,
                                  const flux_subprocess_ops_t *ops,
                                  subprocess_log_f log_fn,
                                  void *log_data);


/* Start / stop a read stream temporarily on local processes.  This
 * may be useful for flow control.  If you desire to have a stream not
 * call 'on_stdout' or 'on_stderr' when the local subprocess has
 * started, see STREAM_STOP configuration above.
 *
 * start and stop return 0 for success, -1 on error
 * status returns > 0 for started, 0 for stopped, -1 on error
 */
int flux_subprocess_stream_start (flux_subprocess_t *p, const char *stream);
int flux_subprocess_stream_stop (flux_subprocess_t *p, const char *stream);
int flux_subprocess_stream_status (flux_subprocess_t *p, const char *stream);

/*
 *  Write data to "stream" stream of subprocess `p`.  'stream' can be
 *  "stdin" or the name of a stream specified with flux_cmd_add_channel().
 *
 *  Returns the total amount of data successfully buffered.
 */
int flux_subprocess_write (flux_subprocess_t *p, const char *stream,
                           const char *buf, size_t len);

/*
 *  Close "stream" stream of subprocess `p` and schedule EOF to be sent.
 *  'stream' can be "stdin" or the name of a stream specified with
 *  flux_cmd_add_channel().
 */
int flux_subprocess_close (flux_subprocess_t *p, const char *stream);

/*
 *  Read up to `len` bytes of unread data from stream `stream`.  To
 *   read all data, specify 'len' of -1.  'stream' can be "stdout",
 *   "stderr", or the name of a stream specified with flux_cmd_add_channel().
 *
 *   Returns pointer to buffer on success and NULL on error with errno
 *   set.  Buffer is guaranteed to be NUL terminated.  User shall not
 *   free returned pointer.  Length of buffer returned can optionally
 *   returned in 'lenp'.  A length of 0 indicates that the subprocess
 *   has closed this stream.
 */
const char *flux_subprocess_read (flux_subprocess_t *p,
                                  const char *stream,
                                  int len, int *lenp);

/*
 *  Read line unread data from stream `stream`.  'stream' can be
 *   "stdout", "stderr", or the name of a stream specified with
 *   flux_cmd_add_channel().
 *
 *   Returns pointer to buffer on success and NULL on error with errno
 *   set.  Buffer will include newline character and is guaranteed to
 *   be NUL terminated.  If no line is available, returns pointer and
 *   length of zero.  User shall not free returned pointer.  Length of
 *   buffer returned can optionally returned in 'lenp'.
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

/* Determine if the read stream has is closed / received an EOF.  This
 * function can be useful if you are reading lines via
 * flux_subprocess_read_line() or flux_subprocess_read_trimmed_line()
 * in output callbacks.  Those functions will return length 0 when no
 * lines are available, making it difficult to determine if the stream
 * has been closed and there is any non-newline terminated data left
 * available for reading with flux_subprocess_read().  Returns > 0 on
 * closed / eof seen, 0 if not, -1 on error.
 */
int flux_subprocess_read_stream_closed (flux_subprocess_t *p,
                                        const char *stream);

/* flux_subprocess_getline() is a special case function
 * that behaves identically to flux_subprocess_read_line() but handles
 * several common special cases.  It requires the stream of data to be
 * line buffered (by default on, see LINE_BUFFER under
 * flux_cmd_setopt()).
 *
 * - if the stream of data has internally completed (i.e. the
 *   subprocess has closed the stream / EOF has been received) but the
 *   last data on the stream does not terminate in a newline
 *   character, this function will return that last data without the
 *   trailing newline.
 * - if the stream has been closed / reached EOF, lenp will be set to
 *   0.
 * - if the stream is not line buffered, NULL and errno = EPERM will
 *   be returned.
 */
const char *flux_subprocess_getline (flux_subprocess_t *p,
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
 *   is destroyed once the last reference is removed.  These calls
 *   silently do nothing if called within a hook.
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

/* Returns the errno causing the FLUX_SUBPROCESS_FAILED state to be reached.
 */
int flux_subprocess_fail_errno (flux_subprocess_t *p);

/* Returns error message describing why FLUX_SUBPROCESS_FAILED state was
 * reached.  If error message was not set, will return strerror() of
 * errno returned from flux_subprocess_fail_errno().
 */
const char *flux_subprocess_fail_error (flux_subprocess_t *p);

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

/*
 *  Take/drop a reference on a subprocess output channel `name` (e.g. "stdout"
 *   or "stderr"). EOF will not be produced from this channel the reference
 *   count drops to zero.
 */
void flux_subprocess_channel_incref (flux_subprocess_t *p, const char *name);
void flux_subprocess_channel_decref (flux_subprocess_t *p, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_SUBPROCESS_H */
