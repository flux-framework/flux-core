/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_SHUTDOWN_H
#define _BROKER_SHUTDOWN_H

struct shutdown;

typedef void (*shutdown_cb_f) (struct shutdown *s, void *arg);

struct shutdown *shutdown_create (flux_t *h,
                                  double grace,
                                  uint32_t size,
                                  int tbon_k,
                                  overlay_t *overlay);
void shutdown_destroy (struct shutdown *s);

void shutdown_set_callback (struct shutdown *s, shutdown_cb_f cb, void *arg);

bool shutdown_is_complete (struct shutdown *s);
bool shutdown_is_expired (struct shutdown *s);

void shutdown_instance (struct shutdown *s);

#endif /* !_BROKER_SHUTDOWN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
