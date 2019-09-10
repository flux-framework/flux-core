/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_PLUGSTACK_H
#define _SHELL_PLUGSTACK_H

#include <flux/core.h>

struct plugstack * plugstack_create (void);

void plugstack_destroy (struct plugstack *st);

int plugstack_push (struct plugstack *st, flux_plugin_t *p);

int plugstack_call (struct plugstack *st,
                    const char *name,
                    flux_plugin_arg_t *args);

#endif /* !_SHELL_PLUGSTACK_H */

/* vi: ts=4 sw=4 expandtab
 */

