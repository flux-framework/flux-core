/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* "bulk" subprocess execution wrapper around libsubprocess API */

#ifndef HAVE_JOB_EXEC_BULK_EXEC_H
#define HAVE_JOB_EXEC_BULK_EXEC_H 1

#include <flux/core.h>

struct bulk_exec;

typedef void (*exec_cb_f)   (struct bulk_exec *, void *arg);

typedef void (*exec_exit_f) (struct bulk_exec *, void *arg,
                             const struct idset *ranks);

typedef void (*exec_io_f)   (struct bulk_exec *,
                             flux_subprocess_t *,
                             const char *stream,
			     const char *data,
			     int data_len,
                             void *arg);

typedef void (*exec_error_f) (struct bulk_exec *,
                              flux_subprocess_t *,
                              void *arg);

struct bulk_exec_ops {
    exec_cb_f    on_start;    /* called when all processes are running  */
    exec_exit_f  on_exit;     /* called when a set of tasks exits       */
    exec_cb_f    on_complete; /* called when all processes are done     */
    exec_io_f    on_output;   /* called on process output               */
    exec_error_f on_error;    /* called on any fatal error              */
};

struct bulk_exec * bulk_exec_create (struct bulk_exec_ops *ops, void *arg);

void *bulk_exec_aux_get (struct bulk_exec *exec, const char *key);

int bulk_exec_aux_set (struct bulk_exec *exec,
                       const char *key,
                       void *val,
                       flux_free_f free_fn);

/*  Set maximum number of flux_subprocess_rexex(3) calls per event
 *   loop iteration. (-1 for no max)
 */
int bulk_exec_set_max_per_loop (struct bulk_exec *exec, int max);

void bulk_exec_destroy (struct bulk_exec *exec);

int bulk_exec_push_cmd (struct bulk_exec *exec,
                       const struct idset *ranks,
                       flux_cmd_t *cmd,
                       int flags);

int bulk_exec_start (flux_t *h, struct bulk_exec *exec);

flux_future_t * bulk_exec_kill (struct bulk_exec *exec, int signal);

int bulk_exec_cancel (struct bulk_exec *exec);

/* Returns max wait status returned from all exited processes */
int bulk_exec_rc (struct bulk_exec *exec);

/* Returns current number of processes starting/running */
int bulk_exec_current (struct bulk_exec *exec);

/* Returns total number of processes expected to run */
int bulk_exec_total (struct bulk_exec *exec);

#endif /* !HAVE_JOB_EXEC_BULK_EXEC_H */
