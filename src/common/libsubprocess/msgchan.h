/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_MSGCHAN_H
#define _SUBPROCESS_MSGCHAN_H

#include <jansson.h>
#include <flux/core.h>

struct msgchan *msgchan_create (flux_reactor_t *r,
                                const char *relay_uri,
                                flux_error_t *error);

void msgchan_destroy (struct msgchan *mch);

// accessors for info to be passed to subprocess
const char *msgchan_get_uri (struct msgchan *mch);
int msgchan_get_fd (struct msgchan *mch);

// for debugging (caller must free stats)
int msgchan_get_stats (struct msgchan *mch, json_t **stats);

#endif /* !_SUBPROCESS_MSGCHAN */

// vi: ts=4 sw=4 expandtab
