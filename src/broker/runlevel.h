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
#    define _BROKER_RUNLEVEL_H

#    include "attr.h"

#    include <stdint.h>
#    include <stddef.h>  // for size_t

typedef struct runlevel runlevel_t;

typedef void (*runlevel_cb_f) (runlevel_t *r,
                               int level,
                               int rc,
                               double elapsed,
                               const char *exit_string,
                               void *arg);
typedef void (*runlevel_io_cb_f) (runlevel_t *r,
                                  const char *name,
                                  const char *msg,
                                  void *arg);

/* Initialize, finalize runlevel calss.
 */
runlevel_t *runlevel_create (void);
int runlevel_register_attrs (runlevel_t *r, attr_t *attr);
void runlevel_set_size (runlevel_t *r, uint32_t size);
void runlevel_set_flux (runlevel_t *r, flux_t *h);
void runlevel_destroy (runlevel_t *r);

/* Handle run level subprocess completion.
 */
void runlevel_set_callback (runlevel_t *r, runlevel_cb_f cb, void *arg);

/* Handle stdout, stderr output lines from subprocesses.
 */
void runlevel_set_io_callback (runlevel_t *r, runlevel_io_cb_f cb, void *arg);

/* Associate 'command' with 'level'.  'local_uri' and 'library_path' are
 * used to set FLUX_URI and LD_LIBRARY_PATH in the subprocess environment.
 */
int runlevel_set_rc (runlevel_t *r,
                     int level,
                     const char *cmd_argz,
                     size_t cmd_argz_len,
                     const char *local_uri);

/* Change the runlevel.  It is assumed that the previous run level (if any)
 * has completed and this is being called from the runlevel callback.
 * Transitions are completely driven by the broker.
 */
int runlevel_set_level (runlevel_t *r, int level);

/* Get the current runlevel.
 */
int runlevel_get_level (runlevel_t *r);

#endif /* !_BROKER_RUNLEVEL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
