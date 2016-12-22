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

#ifndef _SUBPROCESS_H
#define _SUBPROCESS_H

#include <stdbool.h>

struct subprocess_manager;
struct subprocess;

typedef enum sm_item {
    SM_WAIT_FLAGS,
    SM_FLUX,
    SM_REACTOR,
} sm_item_t;


/* Supported hook entry points:
 */
typedef enum {
    SUBPROCESS_PRE_EXEC,   /* In child, just before exec(2)                  */
    SUBPROCESS_POST_FORK,  /* In parent, after fork(2), before child exec(2) */
    SUBPROCESS_RUNNING,    /* In parent, after child exec(2)                 */
    SUBPROCESS_STATUS,     /* Any change in status from waitpid(2)           */
    SUBPROCESS_EXIT,       /* Subprocess has exited and been reaped          */
    SUBPROCESS_COMPLETE,   /* Subprocess has exited and all I/O complete     */
    SUBPROCESS_HOOK_COUNT
} subprocess_hook_t;

/* Prototype for generic subprocess callbacks:
 */
typedef int (*subprocess_cb_f) (struct subprocess *p);

/* I/O specific callback. Output passed in JSON arg
 */
typedef int (*subprocess_io_cb_f) (struct subprocess *p, const char *json_str);

/*
 *  Create a subprocess manager to manage creation, destruction, and
 *   management of subprocess.
 */
struct subprocess_manager * subprocess_manager_create (void);

/*
 *  Set value for item [item] in subprocess manager [sm]
 */
int subprocess_manager_set (struct subprocess_manager *sm,
	sm_item_t item, ...);

/*
 *  Free memory associated with a subprocess manager object:
 */
void subprocess_manager_destroy (struct subprocess_manager *sm);

/*
 *  Execute a new subprocess with arguments argc,argv, and environment
 *   env. The child process will have forked, but not necessarily
 *   executed by the time this function returns.
 */
struct subprocess * subprocess_manager_run (struct subprocess_manager *sm,
	int argc, char *argv[], char **env);

/*
 *  Wait for any child to exit and return the handle of the exited
 *   subprocess to caller.
 */
struct subprocess * subprocess_manager_wait (struct subprocess_manager *sm);

int subprocess_manager_reap_all (struct subprocess_manager *sm);

/*
 *   Get the first subprocess known to subprocess manager [sm].
 */
struct subprocess * subprocess_manager_first (struct subprocess_manager *sm);

/*
 *   Get next subprocess known to subprocess manager [sm]. Returns NULL if
 *    there are no further subprocesses to iterate. Reset iteration with
 *    subprocess_manager_first above.
 *
 */
struct subprocess * subprocess_manager_next (struct subprocess_manager *sm);

/*
 *  Create a new, empty handle for a subprocess object.
 */
struct subprocess * subprocess_create (struct subprocess_manager *sm);

/*
 *  Append function [fn] to the list of callbacks for hook type [hook_type].
 */
int subprocess_add_hook (struct subprocess *p,
			 subprocess_hook_t type,
			 subprocess_cb_f fn);

/*
 *  Set an IO callback
 */
int subprocess_set_io_callback (struct subprocess *p, subprocess_io_cb_f fn);

/*
 *  Destroy a subprocess. Free memory and remove from subprocess
 *   manager list.
 */
void subprocess_destroy (struct subprocess *p);

/*
 *  Set an arbitrary context in the subprocess [p] with name [name].
 */
int subprocess_set_context (struct subprocess *p, const char *name, void *ctx);

/*
 *  Return the saved context for subprocess [p].
 */
void *subprocess_get_context (struct subprocess *p, const char *name);

/*
 *  Set argument vector for subprocess [p]. This function is only valid
 *   before subprocess_run() is called. Any existing args associated with
 *   subprocess are discarded.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_set_args (struct subprocess *p, int argc, char *argv[]);

/*
 *  Set argument vector for subprocess [p] from argz vector (argz, argz_len).
 *   This function is only valid before subprocess_run() is called. Any existing
 *   args associated with subprocess are discarded.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_set_args_from_argz (struct subprocess *p, const char * argz, size_t argz_len);

/*
 *  Identical subprocess_set_args(), subprocess_set_command() is a
 *   convenience function similar to system(3). That is, it will set
 *   arguments for subprocess [p] to
 *
 *     /bin/sh -c "command"
 *
 */
int subprocess_set_command (struct subprocess *p, const char *command);



/*
 *  Append a single argument to the subprocess [p] argument vector.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_argv_append (struct subprocess *p, const char *arg);

/*
 *  Append a single argument to the subprocess [p] argument vector.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_argv_append_argz (struct subprocess *p, const char *argz, size_t argz_len);


/*
 *  Get argument at index [n] from current argv array for process [p]
 *  Returns NULL if n > argc - 1.
 */
const char *subprocess_get_arg (struct subprocess *p, int n);

/*
 *  Return current argument count for subprocess [p].
 */
int subprocess_get_argc (struct subprocess *p);

/*
 *  Set working directory for subprocess [p].
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_set_cwd (struct subprocess *p, const char *cwd);

/*
 *  Get working directory (if any) for subprocess [p].
 *   Returns (NULL) if no working directory is set.
 */
const char *subprocess_get_cwd (struct subprocess *p);

/*
 *  Set initial subprocess environment. This function is only valid
 *   before subprocess_run() is called. Any existing environment array
 *   associated with this subprocess is discarded.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_set_environ (struct subprocess *p, char **env);

/*
 *  Set up a socketpair for communication between parent and child,
 *   and return the parent side of it, or -1 on error (errno set).
 *   Optionally return the child side in 'child_fd' if non-NULL.
 *   It is the caller's responsibility to close the parent fd.
 */
int subprocess_socketpair (struct subprocess *p, int *child_fd);

/*
 *  Setenv() equivalent with optional overwrite for subprocess [p].
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_setenv (struct subprocess *p,
	const char *name, const char *val, int overwrite);

/*
 *   As above but allows formatted args.
 */
int subprocess_setenvf (struct subprocess *p,
	const char *name, int overwrite, const char *fmt, ...);

/*
 *  Unset [name] in the environment array of subprocess [p].
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_unsetenv (struct subprocess *p, const char *name);

/*
 *  getenv(3) equivalent for subprocess [p].
 */
char *subprocess_getenv (struct subprocess *p, const char *name);

/*
 *  Send signal [signo] to subprocess [p]
 */
int subprocess_kill (struct subprocess *p, int signo);

/*
 *  Return PID of process [p] if it is started.
 *   Returns (pid_t) -1 otherwise.
 */
pid_t subprocess_pid (struct subprocess *p);

/*
 *  Wait for and reap the subprocess [p]. After a successful return,
 *   subprocess_exited (p) will be true, and subprocess_exit* will
 *   be valid, etc.
 *  Returns -1 on failure.
 */
int subprocess_reap (struct subprocess *p);

/*
 *  Return 1 if subprocess [p] has exited.
 */
int subprocess_exited (struct subprocess *p);

/*
 *  Return exit status as returned by wait(2) for subprocess [p].
 */
int subprocess_exit_status (struct subprocess *p);

/*
 *  Return exit code for subprocess [p] if !subprocess_signaled()
 */
int subprocess_exit_code (struct subprocess *p);

/*
 *  Return number of the signal that caused process to exit,
 *   or 0 if process was not killed by a signal.
 */
int subprocess_signaled (struct subprocess *p);

/*
 *  Return number of the signal that caused process to stop,
 *   or 0 if process was not stopped.
 */
int subprocess_stopped (struct subprocess *p);

/*
 *  Return 1 if process was continued,
 *   or 0 if process was not contineud.
 */
int subprocess_continued (struct subprocess *p);

/*
 *  If state == "Exec Failure" then return the errno from exec(2)
 *   system call. Otherwise returns 0.
 */
int subprocess_exec_error (struct subprocess *p);

/*
 *  Return string representation of process [p] current state,
 *   "Pending", "Exec Failure", "Waiting", "Running", "Exited"
 */
const char * subprocess_state_string (struct subprocess *p);

/*
 *  Convenience function returning a state string corresponding to
 *   process [p] exit status.
 */
const char * subprocess_exit_string (struct subprocess *p);

/*
 *  Fork, but wait to exec(), subprocess [p].
 *   Returns -1 and EINVAL if subprocess argv is not initialized.
 *           -1 and errno on fork() failure.
 */
int subprocess_fork (struct subprocess *p);

/*
 *   Unblock subprocess [p] and allow it to call exec.
 *    Returns -1 and EINVAL if subprocess is not in state 'started'.
 *            -1 and error if exec failed.
 */
int subprocess_exec (struct subprocess *p);

/*
 *  Same as calling subprocess_fork() + subprocess_exec()
 */
int subprocess_run (struct subprocess *p);


int subprocess_flush_io (struct subprocess *p);

/*
 *  Return 1 if all subprocess stdio has completed (i.e. stdout/stderr
 *   have received and processed EOF). If no IO handler is registered with
 *   a subprocess object then subprocess_io_complete() will always
 *   return 1.
 */
int subprocess_io_complete (struct subprocess *p);

/*
 *  Write data to stdin buffer of process [p]. If [eof] is true then EOF will
 *   be scheduled for stdin once all buffered data is written.
 */
int subprocess_write (struct subprocess *p, void *buf, size_t count, bool eof);

#endif /* !_SUBPROCESS_H */
