/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_RUNLEVEL_H
#define _BROKER_RUNLEVEL_H

#include "attr.h"

#include <stdint.h>
#include <stddef.h> // for size_t

struct runlevel;

typedef void (*runlevel_cb_f)(struct runlevel *r,
                              int level,
                              int rc,
                              double elapsed,
                              const char *exit_string,
                              void *arg);

typedef void (*runlevel_io_cb_f)(struct runlevel *r,
                                 const char *name,
                                 const char *msg,
                                 void *arg);

/* Initialize, finalize runlevel class.
 */
struct runlevel *runlevel_create (flux_t *h, attr_t *attr);
void runlevel_destroy (struct runlevel *r);

/* Handle run level subprocess completion.
 */
void runlevel_set_callback (struct runlevel *r, runlevel_cb_f cb, void *arg);

/* Handle stdout, stderr output lines from subprocesses.
 */
void runlevel_set_io_callback (struct runlevel *r,
                               runlevel_io_cb_f cb,
                               void *arg);

/* Associate 'command' with 'level'.
 * 'local_uri' is used to set FLUX_URI in the subprocess environment.
 */
int runlevel_set_rc (struct runlevel *r,
                     int level,
                     const char *cmd_argz,
                     size_t cmd_argz_len,
                     const char *local_uri);

/* Change the runlevel.  It is assumed that the previous run level (if any)
 * has completed and this is being called from the runlevel callback.
 * Transitions are completely driven by the broker.
 */
int runlevel_set_level (struct runlevel *r, int level);

/* Get the current runlevel.
 */
int runlevel_get_level (struct runlevel *r);

/* Terminate current runlevel.
 * Asynchronously results in runlevel callback, so broker can advance state.
 * If runlevel has no subprocess, callback is immediate with rc=0.
 * Return 0 on success, -1 on failure.
 */
int runlevel_abort (struct runlevel *r);

#endif /* !_BROKER_RUNLEVEL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
