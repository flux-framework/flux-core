/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "get_current_dir_name.h"

char *get_current_dir_name (void)
{
    char buf[PATH_MAX + 1];
    char *name;
    char *cpy;

    if (!(name = getcwd (buf, sizeof (buf)))
        || !(cpy = strdup (name)))
        return NULL;
    return cpy;
}

// vi:ts=4 sw=4 expandtab
