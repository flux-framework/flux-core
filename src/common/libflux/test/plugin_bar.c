/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>

/*  Invalid global symbol to force dlopen failure
 */
extern int my_invalid_sym (void);

int flux_plugin_init (flux_plugin_t *p)
{
    return my_invalid_sym ();
}

/* vi: ts=4 sw=4 expandtab
 */
