/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_COMMAND_H
#define _SUBPROCESS_COMMAND_H

#include <sys/types.h>

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
int flux_cmd_argv_appendf (flux_cmd_t *cmd,
                           const char *fmt, ...)
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
int flux_cmd_setenvf (flux_cmd_t *cmd,
                      int overwrite,
                      const char *name,
                      const char *fmt,
                      ...)
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
 *   Callers can write to the subprocess via flux_subprocess_write()
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
 *   The BUFSIZE string may be a floating point quantity scaled by
 *   an optional suffix from the set 'kKMG'.
 *
 *  "LINE_BUFFER" option
 *
 *    By default, the 'on_output' callback
 *    is called when a line of data is available (with the exception
 *    with data after a subprocess has exited).  By setting this
 *    option to "false", the output callback will be called whenever any amount
 *    of data is available on the stream.  These options can also be set to
 *    "true" to keep default behavior of line buffering.
 *
 *    - name + "_LINE_BUFFER" - configuring line buffering on channel name
 *    - stdout_LINE_BUFFER - configure line buffering for stdout
 *    - stderr_LINE_BUFFER - configure line buffering for stderr
 */
int flux_cmd_setopt (flux_cmd_t *cmd, const char *var, const char *val);
const char *flux_cmd_getopt (flux_cmd_t *cmd, const char *var);

/*  Request a message channel for communication between process and caller.
 *
 *  The 'name' argument is the name of an environment variable that will be
 *   set to a fd:// URI on the process end.  The file descriptor in the path
 *   component of this URI represents one end of a socketpair opened in the
 *   caller and passed to the process.
 *
 *  The 'uri' argument is normally an interthread:// connector URI that will
 *   represent the channel on the caller end and act as message buffer.
 *   The rules of interthread:// are that a given URI may be flux_open()ed up
 *   to twice within the same process address space, in any order.  The
 *   subprocess object will be one opener for the specified URI.  The caller
 *   may be the other opener, or the URI may be safely passed to another
 *   thread and opened there.  The interthread URI should be chosen so it
 *   doesn't conflict with other URIs in the same process address space.
 *
 *  The two URIs will be joined back-to-back, so messages sent on one end will
 *   appear on the other end.
 */
int flux_cmd_add_message_channel (flux_cmd_t *cmd,
                                  const char *name,
                                  const char *uri);


/*  Set a string "label" for this command that can be used to refer to it
 *  in other API calls.
 */
int flux_cmd_set_label (flux_cmd_t *cmd, const char *label);
const char *flux_cmd_get_label (flux_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* !_SUBPROCESS_COMMAND_H */

// vi: ts=4 sw=4 expandtab
