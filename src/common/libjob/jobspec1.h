/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_JOBSPEC1_H
#define _FLUX_CORE_JOBSPEC1_H

#include <flux/core.h>

typedef struct flux_jobspec1 flux_jobspec1_t;
typedef flux_error_t flux_jobspec1_error_t;

/* Remove the value in the jobspec's attributes section at the given path,
 * where "." is treated as a path separator.
 * It is not an error if 'path' does not exist.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_attr_del (flux_jobspec1_t *jobspec, const char *path);

/* Set the value in the jobspec's attributes section at the given path,
 * where "." is treated as a path separator.  'fmt' should be a jansson
 * pack-style string corresponding to the remaining arguments.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_attr_pack (flux_jobspec1_t *jobspec,
                             const char *path,
                             const char *fmt,
                             ...);

/* Unpack the values in the jobspec's attributes section at the given path,
 * where "." is treated as a path separator.  'fmt' should be a jansson
 * unpack-style string corresponding to the remaining args.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_attr_unpack (flux_jobspec1_t *jobspec,
                               const char *path,
                               const char *fmt,
                               ...);

/* Check the validity of only the attributes section of a jobspec, sufficient
 * if the jobspec object was created by flux_jobspec1_from_command(), then
 * modified with the other jobspec1 functions.
 * Return 0 on success, -1 on error with errno set.
 * On error, write an error message written to 'error', if non-NULL.
 */
int flux_jobspec1_attr_check (flux_jobspec1_t *jobspec,
                              flux_jobspec1_error_t *error);

/* Check the validity of the full jobspec, which may be necessary if the
 * jobspec object was created by flux_jobspec1_decode().
 * Return 0 on success, -1 on error with errno set.
 * On error, write an error message written to 'error', if non-NULL.
 */
int flux_jobspec1_check (flux_jobspec1_t *jobspec,
                         flux_jobspec1_error_t *error);

/* Remove the variable 'name' from the environment.
 * This function succeeds if 'name' does not exist.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_unsetenv (flux_jobspec1_t *jobspec, const char *name);

/* Add the variable 'name' to the environment with the value 'value'.
 * If 'name' exists in the environment and 'overwrite' is nonzero, change
 * value to 'value'.  If 'name' exists and 'overwrite' is zero, do not
 * change the value (and return success).
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_setenv (flux_jobspec1_t *jobspec,
                           const char *name,
                           const char *value,
                           int overwrite);

/* Redirect job stdin from the KVS to a file system path given by 'path'.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_set_stdin (flux_jobspec1_t *jobspec, const char *path);

/* Redirect job stdout from the KVS to a file system path given by 'path'.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_set_stdout (flux_jobspec1_t *jobspec, const char *path);

/* Redirect job stderr from the KVS to a file system path given by 'path'.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_set_stderr (flux_jobspec1_t *jobspec, const char *path);

/* Set the working directory of a jobspec.
 * Return 0 on success, -1 on error with errno set.
 */
int flux_jobspec1_set_cwd (flux_jobspec1_t *jobspec, const char *cwd);

/* Encode a jobspec to a string, e.g. for usage with flux_job_submit().
 * 'flags' should be 0.  The returned string must be released with free().
 * Return NULL on error with errno set.
 */
char *flux_jobspec1_encode (flux_jobspec1_t *jobspec, size_t flags);

/* Decode a string to jobspec.  No validation is performed on the content.
 * The returned jobspec must be released with flux_jobspec1_destroy().
 * Return NULL on error with errno set.
 * On error, write an error message to 'error', if non-NULL.
 */
flux_jobspec1_t *flux_jobspec1_decode (const char *s,
                                       flux_jobspec1_error_t *error);


/* Create and return a minimum viable V1 Jobspec.
 * The jobspec must be released with flux_jobspec1_destroy()'
 * The jobspec will have stdin, stdout, stderr all directed to the KVS.
 * 'argc' and 'argv' define the command and its arguments.
 * 'env' should be an environ(7)-style array, or NULL for empty.
 * 'tasks' should be the number of tasks to launch
 * 'cores_per_task' should be the number of cores per task to allocate
 * 'gpus_per_task' should be the number of gpus per task to allocate
 * 'nodes' should be the number of nodes to spread the allocated cores
 * and gpus across. If 0, the scheduler will determine the node allocation.
 * Return NULL on error with errno set.
 */
flux_jobspec1_t *flux_jobspec1_from_command (int argc,
                                             char **argv,
                                             char **env,
                                             int ntasks,
                                             int cores_per_task,
                                             int gpus_per_task,
                                             int nnodes,
                                             double duration);

/* Free a jobspec. */
void flux_jobspec1_destroy (flux_jobspec1_t *jobspec);

#endif /* _FLUX_CORE_JOBSPEC1_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
