/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

/* Validate that hardwired arguments were passed.
 * Return 0 on success, -1 on failure with errno set.
 */
int mod_main (void *ctx, int argc, char *argv[])
{
    if (argc != 2 || strcmp (argv[0], "foo=42") != 0
                  || strcmp (argv[1], "bar=abcd") != 0)
        return -1;
    return 0;
}

/* This dso extends a comms module named "parent".
 */
MOD_NAME ("parent.child");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
