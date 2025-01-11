/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_SHELL_H
#define FLUX_SHELL_H

#include <flux/core.h>
#include <flux/taskmap.h>
#include <flux/hostlist.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_shell flux_shell_t;
typedef struct shell_task flux_shell_task_t;

typedef void (*flux_shell_task_io_f) (flux_shell_task_t *task,
                                      const char *stream,
                                      void *arg);

/*  Get flux_shell_t object from flux plugin handle
 */
flux_shell_t * flux_plugin_get_shell (flux_plugin_t *p);

/*  Get flux handle from shell
 */
flux_t * flux_shell_get_flux (flux_shell_t *shell);

/* flux_shell_t interface */

int flux_shell_aux_set (flux_shell_t *shell,
                        const char *name,
                        void *aux,
                        flux_free_f free_fn);

void * flux_shell_aux_get (flux_shell_t *shell, const char *name);

/*  Get shell option as JSON string as set in jobspec
 *   attributes.system.shell.options
 *
 *  Returns 1 on success, 0 if option "name" was not set, or -1 on failure.
 */
int flux_shell_getopt (flux_shell_t *shell, const char *name, char **json_str);

/*  Unpack shell option from attributes.system.shell.options.<name> using
 *   jansson style unpack arguments.
 *
 *  Returns 1 on success, 0 if option was not found, or -1 on error.
 */
int flux_shell_getopt_unpack (flux_shell_t *shell,
                              const char *name,
                              const char *fmt, ...);

/*  Set shell option as a JSON string. Future calls to getopt will return
 *   value encoded in the JSON string. If json_str is NULL, the option
 *   will unset.
 *
 *  Returns 0 on success, -1 on error.
 */
int flux_shell_setopt (flux_shell_t *shell,
                       const char *name,
                       const char *json_str);

/*  Set shell option using jansson style pack args
 *
 *  Returns 0 on success, -1 on error.
 */
int flux_shell_setopt_pack (flux_shell_t *shell,
                            const char *name,
                            const char *fmt, ...);

/*  Get value of an environment variable from the global job environment
 */
const char * flux_shell_getenv (flux_shell_t *shell, const char *name);

/*  Get environ job environment as a JSON string. Caller must free.
 */
int flux_shell_get_environ (flux_shell_t *shell, char **json_str);

/*  Set an environment variable in the global job environment
 */
int flux_shell_setenvf (flux_shell_t *shell,
                        int overwrite,
                        const char *name,
                        const char *fmt, ...)
                        __attribute__ ((format (printf, 4, 5)));

/*  Unset an environment variable in the global job environment
 */
int flux_shell_unsetenv (flux_shell_t *shell, const char *name);

/*  Return the job shell's cached copy of hwloc XML.
 */
int flux_shell_get_hwloc_xml (flux_shell_t *shell, const char **xmlp);

/*  Return the current shell taskmap
 */
const struct taskmap *flux_shell_get_taskmap (flux_shell_t *shell);

/*  Return the list of hosts assigned to this job as a hostlist
 */
const struct hostlist *flux_shell_get_hostlist (flux_shell_t *shell);

/*  Return shell info as a JSON string.
 *  {
 *   "jobid":I,
 *   "instance_owner":i,
 *   "rank":i,
 *   "size":i,
 *   "ntasks";i,
 *   "service":s,
 *   "options": { "verbose":b },
 *   "jobspec":o,
 *   "R":o
 *  }
 */
int flux_shell_get_info (flux_shell_t *shell, char **json_str);


/*  Access shell info object with Jansson-style unpack args.
 */
int flux_shell_info_unpack (flux_shell_t *shell,
                            const char *fmt, ...);

/*  Return rank and task info for given shell rank as JSON string.
 *  {
 *   "broker_rank":i,
 *   "id":i,      // same as shell_rank parameter
 *   "name":s,    // hostname of this shell_rank
 *   "ntasks":i
 *   "taskids": s // task id list for this rank in RFC 22 idset form.
 *   "resources": { "ncores":i, "cores":s, ... }
 *  }
 */
int flux_shell_get_rank_info (flux_shell_t *shell,
                              int shell_rank,
                              char **json_str);

/*  Access shell rank info object with Jansson-style unpack args.
 */
int flux_shell_rank_info_unpack (flux_shell_t *shell,
                                 int shell_rank,
                                 const char *fmt, ...);

/*  Return summary information about jobspec
 *
 *  The only required member of the JSON object is a version number,
 *   indicating the version of the jobspec for the current job:
 *
 *  {
 *   "version":i         # jobspec version number
 *  }
 *
 *  For jobspec version 1, the following keys are also provided:
 *  {
 *   "ntasks":i,         # number of tasks requested
 *   "nslots":i,         # number of task slots
 *   "cores_per_slot":i  # number of cores per task slot
 *   "nnodes":i          # number of nodes requested, -1 if unset
 *   "slots_per_node":i  # number of slots per node, -1 if unavailable
 *  }
 *
 */
int flux_shell_get_jobspec_info (flux_shell_t *shell, char **json_str);


/*  Access jobspec info object with Jansson-style unpack args
 */
int flux_shell_jobspec_info_unpack (flux_shell_t *shell,
                                    const char *fmt, ...);

/*
 *  Take a "completion reference" on the shell object `shell`.
 *  This function takes a named reference on the shell so that it will
 *  not consider a job "complete" until the reference is released with
 *  flux_shell_remove_completion_ref().
 *
 *  Returns the reference count for the particular name, or -1 on error.
 */
int flux_shell_add_completion_ref (flux_shell_t *shell,
                                   const char *fmt, ...)
                                   __attribute__ ((format (printf, 2, 3)));

/*
 *  Remove a named "completion reference" for the shell object `shell`.
 *  Once all references have been removed, the shells reactor is stopped
 *  with flux_reactor_stop (shell->r).
 *
 *  Returns 0 on success, -1 on failure.
 */
int flux_shell_remove_completion_ref (flux_shell_t *shell,
                                      const char *fmt, ...)
                                      __attribute__ ((format (printf, 2, 3)));

/*  Send signal `sig` to all currently running tasks in shell.
 */
void flux_shell_killall (flux_shell_t *shell, int sig);


/*  Add an event handler for shell event `subtopic`. Message handler
 *  will be auto-destroyed on flux handle destruction at shell shutdown.
 */
int flux_shell_add_event_handler (flux_shell_t *shell,
                                  const char *subtopic,
                                  flux_msg_handler_f cb,
                                  void *arg);

/*  Register service handler for `method` with the shell
 */
int flux_shell_service_register (flux_shell_t *shell,
                                 const char *method,
                                 flux_msg_handler_f cb,
                                 void *arg);

/*  Send an rpc to shell `method` by shell rank
 */
flux_future_t *flux_shell_rpc_pack (flux_shell_t *shell,
                                    const char *method,
                                    int shell_rank,
                                    int flags,
                                    const char *fmt, ...);

/*  Call into shell plugin stack
 */
int flux_shell_plugstack_call (flux_shell_t *shell,
                               const char *topic,
                               flux_plugin_arg_t *args);

/*  Add context information for standard shell events
 */
int flux_shell_add_event_context (flux_shell_t *shell,
                                  const char *name,
                                  int flags,
                                  const char *fmt, ...);

/*  flux_shell_task_t API:
 */

/*  Return the current task for task_init, task_exec, and task_exit callbacks:
 *
 *  Returns NULL in any other context.
 */
flux_shell_task_t * flux_shell_current_task (flux_shell_t *shell);

/*  Iterate over all shell tasks:
 */
flux_shell_task_t *flux_shell_task_first (flux_shell_t *shell);
flux_shell_task_t *flux_shell_task_next (flux_shell_t *shell);

/*  Return task general information as JSON string:
 *  {
 *    "localid":i,
 *    "rank":i,
 *    "state":s,
 *    "pid":I,
 *    "wait_status":i,
 *    "exitcode":i,
 *    "signaled":i
 *  }
 *  Where 'pid' is only valid when task is running, and 'wait_status',
 *  'exitcode' and 'signaled' are only valid when task has exited.
 */
int flux_shell_task_get_info (flux_shell_task_t *task, char **json_str);

/*  Get shell task info with unpack-style args.
 */
int flux_shell_task_info_unpack (flux_shell_task_t *task,
                                 const char *fmt, ...);

/*
 *  Return the cmd structure for a shell task.
 */
flux_cmd_t *flux_shell_task_cmd (flux_shell_task_t *task);

/*
 *  Return the subprocess for a shell task in task_fork, task_exit callbacks:
 */
flux_subprocess_t *flux_shell_task_subprocess (flux_shell_task_t *task);

/*  Call `cb` when channel `name` is ready for reading.
 *
 *  Callback can then call flux_shell_task_get_subprocess() and use
 *   flux_subprocess_read() or getline() on the result to get
 *   available data.
 *
 *  Only one subscriber per stream is allowed. If subscribe is called
 *   on a stream with an existing subscriber then -1 is returned with
 *   errno set to EEXIST.
 */
int flux_shell_task_channel_subscribe (flux_shell_task_t *task,
                                       const char *channel,
                                       flux_shell_task_io_f cb,
                                       void *arg);

/* shell logging functions:
 *
 * Logging levels are remapped from Internet RFC 5452 severity levels,
 *  where here the enumeration identifiers are renamed to make sense
 *  in the context of shell logging.
 */
enum {
    FLUX_SHELL_QUIET  = -1,
    FLUX_SHELL_FATAL  = 0,  /* LOG_EMERG   */
    /* Level 1 Reserved */  /* LOG_ALERT   */
    /* Level 2 Reserved */  /* LOG_CRIT    */
    FLUX_SHELL_ERROR  = 3,  /* LOG_ERR     */
    FLUX_SHELL_WARN   = 4,  /* LOG_WARNING */
    FLUX_SHELL_NOTICE = 5,  /* LOG_NOTICE  */
    FLUX_SHELL_DEBUG  = 6,  /* LOG_INFO    */
    FLUX_SHELL_TRACE  = 7,  /* LOG_DEBUG   */
};

#ifndef FLUX_SHELL_PLUGIN_NAME
# error "FLUX_SHELL_PLUGIN_NAME must be defined"
#endif

#define shell_trace(...) \
    flux_shell_log (FLUX_SHELL_PLUGIN_NAME, \
                    FLUX_SHELL_TRACE, __FILE__, __LINE__, __VA_ARGS__)

#define shell_debug(...) \
    flux_shell_log (FLUX_SHELL_PLUGIN_NAME, \
                    FLUX_SHELL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#define shell_log(...) \
    flux_shell_log (FLUX_SHELL_PLUGIN_NAME, \
                    FLUX_SHELL_NOTICE, __FILE__, __LINE__, __VA_ARGS__)

#define shell_warn(...) \
    flux_shell_log (FLUX_SHELL_PLUGIN_NAME, \
                    FLUX_SHELL_WARN, __FILE__, __LINE__, __VA_ARGS__)

#define shell_log_error(...) \
    flux_shell_log (FLUX_SHELL_PLUGIN_NAME, \
                    FLUX_SHELL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#define shell_log_errn(errn, ...) \
    flux_shell_err (FLUX_SHELL_PLUGIN_NAME, \
                    __FILE__, __LINE__, errn, __VA_ARGS__)

#define shell_log_errno(...) \
    flux_shell_err (FLUX_SHELL_PLUGIN_NAME, \
                    __FILE__, __LINE__, errno, __VA_ARGS__)

#define shell_die(code,...) \
    flux_shell_fatal (FLUX_SHELL_PLUGIN_NAME, \
                      __FILE__, __LINE__, \
                      0, code, __VA_ARGS__)

#define shell_die_errno(code,...) \
    flux_shell_fatal (FLUX_SHELL_PLUGIN_NAME, \
                      __FILE__, __LINE__, \
                      errno, code, __VA_ARGS__)

#define shell_set_verbose(n) \
    flux_shell_log_setlevel(FLUX_SHELL_NOTICE+n, NULL)

#define shell_set_quiet(n) \
    flux_shell_log_setlevel(FLUX_SHELL_NOTICE-n, NULL)

/*  Log a message at level to all registered loggers at level or above
 */
void flux_shell_log (const char *component,
                     int level,
                     const char *file,
                     int line,
                     const char *fmt, ...)
                     __attribute__ ((format (printf, 5, 6)));

/*  Log a message at FLUX_SHELL_ERROR level, additionally appending the
 *   result of strerror (errnum) for convenience.
 *
 *  Returns -1 with errno = errnum, so that the function can be used as
 *   return flux_shell_err (...);
 */
int flux_shell_err (const char *component,
                    const char *file,
                    int line,
                    int errnum,
                    const char *fmt, ...)
                    __attribute__ ((format (printf, 5, 6)));

/*  Log a message at FLUX_SHELL_FATAL level and schedule termination of
 *   the job shell. May generate an exception if tasks are already
 *   running. Exits with exit_code.
 */
void flux_shell_fatal (const char *component,
                       const char *file,
                       int line,
                       int errnum,
                       int exit_code,
                       const char *fmt, ...)
                       __attribute__ ((format (printf, 6, 7)));

void flux_shell_raise (const char *type, int severity, const char *fmt, ...);

/*  Set default severity of logging destination 'dest' to level.
 *   If dest == NULL then set the internal log dispatch level --
 *   (i.e. no messages above severity level will be logged to any
 *    log destination)
 *
 *  If 'level' is FLUX_SHELL_QUIET, then logging to 'dest' is disabled.
 */
int flux_shell_log_setlevel (int level, const char *dest);

/*  Expand mustache template.  Caller must free the result.
 */
char *flux_shell_mustache_render (flux_shell_t *shell, const char *fmt);

/*  Same as flux_shell_mustache_render(3), but render for an alternate
 *  shell rank. Caller must free result.
 */
char *flux_shell_rank_mustache_render (flux_shell_t *shell,
                                       int shell_rank,
                                       const char *fmt);

/*  Same as flux_shell_mustache_render(3), but render for a specific task.
 *  Caller must free result.
 */
char *flux_shell_task_mustache_render (flux_shell_t *shell,
                                       flux_shell_task_t *task,
                                       const char *fmt);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_SHELL_H */
