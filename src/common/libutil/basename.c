/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <string.h>

#include "basename.h"

/* This is what glibc basename(3) does more or less.
 * https://github.com/lattera/glibc/blob/master/string/basename.c
 */

char *basename_simple (const char *path)
{
    char *p = strrchr (path, '/');
    return p ? p + 1 : (char *)path;
}

// vi:ts=4 sw=4 expandtab
