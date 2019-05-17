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
#    include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>

#include <czmq.h>
#include "unlink_recursive.h"
#include "dirwalk.h"

static int unlinker (dirwalk_t *d, void *arg)
{
    if (unlinkat (dirwalk_dirfd (d),
                  dirwalk_name (d),
                  dirwalk_isdir (d) ? AT_REMOVEDIR : 0)
        < 0)
        dirwalk_stop (d, errno);
    return (0);
}

int unlink_recursive (const char *dirpath)
{
    return dirwalk (dirpath, DIRWALK_DEPTH, unlinker, NULL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
