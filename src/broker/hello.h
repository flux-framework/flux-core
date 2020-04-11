/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_HELLO_H
#define _BROKER_HELLO_H

#include <stdbool.h>
#include "attr.h"

/* hello protocol is used to detect that TBON overlay has wired up.
 */

struct hello;

typedef void (*hello_cb_f)(struct hello *hello, void *arg);

struct hello *hello_create (void);
void hello_destroy (struct hello *hello);

/* Register handle
 */
void hello_set_flux (struct hello *hello, flux_t *h);

/* Set up broker attributes
 */
int hello_register_attrs (struct hello *hello, attr_t *attrs);

/* Register callback for completion/progress.
 */
void hello_set_callback (struct hello *hello, hello_cb_f cb, void *arg);

/* Get time in seconds elapsed since hello_start()
 */
double hello_get_time (struct hello *hello);

/* Get number of ranks currently accounted for.
 */
int hello_get_count (struct hello *hello);

/* Get completion status
 */
bool hello_complete (struct hello *hello);

/* Start the hello protocol (call on all ranks).
 */
int hello_start (struct hello *hello);

#endif /* !_BROKER_HELLO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
