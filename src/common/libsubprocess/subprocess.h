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

#include "command.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    /* let parent stdin, stdout, stderr, carry to
     * child.  Do not create "stdin", "stdout", or "stderr" channels.
     * Subsequently, flux_subprocess_write()/close()/read()/read_line()
     * will fail on streams of "stdin", "stdout", or "stderr".
     */
    FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH = 1,
    /* do not call setpgrp() before exec(2) */
    FLUX_SUBPROCESS_FLAGS_NO_SETPGRP = 2,
    /* use fork(2)/exec(2) even if posix_spawn(3)
     * available */
    FLUX_SUBPROCESS_FLAGS_FORK_EXEC = 4,
    /* flux_rexec() only: In order to improve performance, do not locally
     * copy and buffer output from the remote subprocess.  Immediately
     * call output callbacks.  Users should call
     * flux_subprocess_read() to retrieve the data.  If
     * flux_subprocess_read() is not called, data will be lost.  Data
     * will not be NUL terminated.  flux_subprocess_read() should be
     * called only once.  If called more than once, the same data is
     * returned.  Use of other read functions will result in a EPERM
     * error.
     */
    FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF = 8,
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
typedef void (*flux_subprocess_credit_f) (flux_subprocess_t *p,
                                          const char *stream,
                                          int bytes);
typedef void (*flux_subprocess_hook_f) (flux_subprocess_t *p, void *arg);

/*
 *  Functions for event-driven subprocess handling:
 *
 *  When output callbacks are called, flux_subprocess_read(),
 *  flux_subprocess_read_line() and similar functions should be used
 *  to read buffered data.  If this is not done, it can lead to
 *  excessive callbacks and code "spinning".
 *
 *  The first call to on_credit will contain the full buffer size.
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
    flux_subprocess_credit_f on_credit; /* Write buffer space available      */
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
typedef void (*subprocess_log_f)(void *arg,
                                 const char *file,
                                 int line,
                                 const char *func,
                                 const char *subsys,
                                 int level,
                                 const char *fmt,
                                 va_list args);


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

flux_subprocess_t *flux_rexec (flux_t *h,
                               int rank,
                               int flags,
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

/* Like flux_rexec(3), but run process in background.
 *
 * If service_name is NULL, then default to `rexec`.
 *
 * Once the process has been started, the returned future will be fulfilled
 * with the payload ``{"rank":i, "pid":i}``, or an error response if there
 * is an error.
 */
flux_future_t *flux_rexec_bg (flux_t *h,
                              const char *service_name,
                              int rank,
                              int flags,
                              const flux_cmd_t *cmd);

/* Start / stop a read stream temporarily on local processes.  This
 * may be useful for flow control.
 */
void flux_subprocess_stream_start (flux_subprocess_t *p, const char *stream);
void flux_subprocess_stream_stop (flux_subprocess_t *p, const char *stream);

/*
 *  Write data to "stream" stream of subprocess `p`.  'stream' can be
 *  "stdin" or the name of a stream specified with flux_cmd_add_channel().
 *
 *  Returns the total amount of data successfully buffered or -1 on error
 *  with errno set. Note: this function will not return a short write. If
 *  all `len` bytes cannot fit in the destination buffer, then no bytes
 *  will be written and -1 will be returned with errno=ENOSPC.
 */
int flux_subprocess_write (flux_subprocess_t *p,
                           const char *stream,
                           const char *buf,
                           size_t len);

/*
 *  Close "stream" stream of subprocess `p` and schedule EOF to be sent.
 *  'stream' can be "stdin" or the name of a stream specified with
 *  flux_cmd_add_channel().
 */
int flux_subprocess_close (flux_subprocess_t *p, const char *stream);

/*
 *  Read unread data from stream `stream`.  'stream' can be "stdout",
 *   "stderr", or the name of a stream specified with flux_cmd_add_channel().
 *
 *   Returns length of data on success and -1 on error with errno set.
 *   Buffer of data is returned in bufp.  Buffer is guaranteed to be
 *   NUL terminated unless the FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF flag
 *   has been specified.  User shall not free returned pointer.
 *
 *   In most cases, a length of 0 indicates that the subprocess has
 *   closed this stream.  A length of 0 could be returned if read
 *   functions are called multiple times within a single output
 *   callback, so it is generally recommended to call this function
 *   once per output callback.  flux_subprocess_read_stream_closed()
 *   can always be used to verify if the stream is in fact closed.
 */
int flux_subprocess_read (flux_subprocess_t *p,
                          const char *stream,
                          const char **bufp);

/*
 *  Read line of unread data from stream `stream`.  'stream' can be
 *   "stdout", "stderr", or the name of a stream specified with
 *   flux_cmd_add_channel().
 *
 *   Returns length of data on success and -1 on error with errno set.
 *   Buffer with line is returned in bufp.  Buffer will include
 *   newline character and is guaranteed to be NUL terminated.  If no
 *   line is available, returns length of zero.  User shall not free
 *   returned pointer.
 *
 *   A length of zero may be returned if the stream is closed OR if
 *   the stream is line buffered and a line is not yet available. Use
 *   flux_subprocess_read_stream_closed() to distinguish between the
 *   two.
 *
 *   This function may return an incomplete line when:
 *
 *   1) the stream has closed and the last output is not a line
 *   2) a single line of output exceeds the size of an internal output
 *      buffer (see BUFSIZE option).
 */
int flux_subprocess_read_line (flux_subprocess_t *p,
                               const char *stream,
                               const char **bufp);

/* Identical to flux_subprocess_read_line(), but does not return
 * trailing newline.
 */
int flux_subprocess_read_trimmed_line (flux_subprocess_t *p,
                                       const char *stream,
                                       const char **bufp);

/* Determine if the read stream has is closed / received an EOF.  This
 * function can be useful if you are reading lines via
 * flux_subprocess_read_line() or flux_subprocess_read_trimmed_line()
 * in output callbacks.  Those functions will return length 0 when no
 * lines are available, making it difficult to determine if the stream
 * has been closed and there is any non-newline terminated data left
 * available for reading with flux_subprocess_read().
 */
bool flux_subprocess_read_stream_closed (flux_subprocess_t *p,
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
 * - if the stream has been closed / reached EOF, 0 will be returned.
 * - if the stream is not line buffered, NULL and errno = EPERM will
 *   be returned.
 */
int flux_subprocess_getline (flux_subprocess_t *p,
                             const char *stream,
                             const char **bufp);

/*
 *  Create RPC to send signal `signo` to subprocess `p`.
 *  This call returns a flux_future_t. Use flux_future_then(3) to register
 *   a continuation callback when the kill operation is complete, or
 *   flux_future_wait_for(3) to block until the kill operation is complete.
 */
flux_future_t *flux_subprocess_kill (flux_subprocess_t *p, int signo);

/*
 *  Remove a reference to subprocess object `p`. The subprocess object
 *   is destroyed once the last reference is removed.  This call
 *   silently deso nothing if called within a hook.
 */
void flux_subprocess_destroy (flux_subprocess_t *p);

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

/*  Return true if subprocess p is still active, i.e. it is waiting to
 *  start, still running, or waiting for eof on all streams.
 */
bool flux_subprocess_active (flux_subprocess_t *p);

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
                             const char *name,
                             void *ctx,
                             flux_free_f free);

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

// vi: ts=4 sw=4 expandtab
