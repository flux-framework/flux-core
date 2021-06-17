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

typedef struct flux_jobspec1 flux_jobspec1_t;

/* Remove the value in the jobspec's 'attributes' section at the given path,
 * where "." is treated as a path separator. Return 0 on success, -1 on error.
 * It is not an error if 'path' does not exist.
 */
int flux_jobspec1_attr_del (flux_jobspec1_t *jobspec, const char *path);

/* Set the value in the jobspec's 'attributes' section at the given path,
 * where "." is treated as a path separator.
 * 'fmt' should be a jansson pack-style string corresponding to the
 * remaining arguments.
 * Return 0 on success, -1 on error.
 */
int flux_jobspec1_attr_pack (flux_jobspec1_t *jobspec,
                             const char *path,
                             const char *fmt,
                             ...);

/* Unpack the values in the jobspec's 'attributes' section at the given path,
 * where "." is treated as a path separator.
 * 'fmt' should be a jansson unpack-style string corresponding to the
 * remaining args.
 * Return 0 on success, -1 on error.
 */
int flux_jobspec1_attr_unpack (flux_jobspec1_t *jobspec,
                               const char *path,
                               const char *fmt,
                               ...);

/* Check the validity of the 'attributes' section of a jobspec,
 * writing an error message to 'errbuf'.
 * Return 0 on success, -1 on error with an error message written.
 * This function succeeding does not guarantee that the jobspec is valid.
 */
int flux_jobspec1_attr_check (flux_jobspec1_t *jobspec,
                              char *errbuf,
                              int errbufsz);

/* Remove an entry in a Jobspec's environment.
 * This function succeeds regardless of the presence of the variable.
 * However, it will fail if the environment object does not exist.
 * Return 0 on success, nonzero on error.
 */
int flux_jobspec1_unsetenv (flux_jobspec1_t *jobspec, const char *name);

/* Add the variable 'name' to the environment
 * with the value 'value', if name does not already exist.  If 'name'
 * does exist in the environment, then its value is changed to 'value'
 * if 'overwrite' is nonzero; if 'overwrite' is zero, then the value of
 *'name' is not changed (and this function returns a success status).
 * Return 0 on success, nonzero with errno set on error.
 */
int flux_jobspec1_setenv (flux_jobspec1_t *jobspec,
                           const char *name,
                           const char *value,
                           int overwrite);

/* Direct the stdin of a jobspec to a path given by
 * 'stdin'. Return 0 on success, -1 on error.
 */
int flux_jobspec1_set_stdin (flux_jobspec1_t *jobspec, const char *path);

/* Direct the stdout of a jobspec to a path given by
 * 'stdout'. Return 0 on success, -1 on error.
 */
int flux_jobspec1_set_stdout (flux_jobspec1_t *jobspec, const char *path);

/* Direct the stderr of a jobspec to a path given by
 * 'stderr'. Return 0 on success, -1 on error.
 */
int flux_jobspec1_set_stderr (flux_jobspec1_t *jobspec, const char *path);

/* Set the working directory of a jobspec.
 * Return 0 on success, -1 on error.
 */
int flux_jobspec1_set_cwd (flux_jobspec1_t *jobspec, const char *cwd);

/* Encode a jobspec to a string, e.g. for usage with ``flux_job_submit()``.
 * 'flags' should be 0.
 * Return NULL with errno set on error.
 * The return value must be released with ``free()``.
 */
char *flux_jobspec1_encode (flux_jobspec1_t *jobspec, size_t flags);

/* Create and return a minimum viable V1 Jobspec.
 * The jobspec will have stdin, stdout, stderr all directed to the KVS,
 * and the environment will be empty.
 * The jobspec must be released with 'flux_jobspec1_free'.
 * 'argc' and 'argv' should collectively define the command and its
 * arguments.
 * 'env' should be an 'environ(7)'-style array, where NULL indicates an
 * empty environment.
 * 'tasks' should be the number of tasks to launch
 * 'cores_per_task' should be the number of cores per task to allocate
 * 'gpus_per_task' should be the number of gpus per task to allocate
 * 'nodes' should be the number of nodes to spread the allocated cores
 * and gpus across. If 0, the scheduler will determine how to distribute
 * the resources.
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
