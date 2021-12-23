/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_SERVHASH_H
#define _ROUTER_SERVHASH_H

#include <flux/core.h>

struct servhash;

typedef void (*respond_f)(const flux_msg_t *msg,
                          const char *uuid,
                          int errnum,
                          void *arg);

void servhash_set_respond (struct servhash *sh, respond_f cb, void *arg);

// Avoid service.remove deadlock during broker shutdown - like issue #1025
void servhash_mute (struct servhash *sh);

struct servhash *servhash_create (flux_t *h);
void servhash_destroy (struct servhash *sh);

int servhash_add (struct servhash *sh,
                  const char *name,
                  const char *uuid,
                  flux_msg_t *msg);

int servhash_remove (struct servhash *sh,
                     const char *name,
                     const char *uuid,
                     flux_msg_t *msg);

int servhash_match (struct servhash *sh,
                    const flux_msg_t *msg,
                    const char **uuid);

void servhash_disconnect (struct servhash *sh, const char *uuid);

int servhash_renew (struct servhash *sh);

#endif /* !_ROUTER_SERVHASH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
