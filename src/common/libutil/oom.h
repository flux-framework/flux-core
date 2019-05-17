/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_OOM_H
#    define _UTIL_OOM_H

#    include <stdio.h>

#    define oom()                                          \
        do {                                               \
            fprintf (stderr,                               \
                     "%s::%s(), line %d: Out of memory\n", \
                     __FILE__,                             \
                     __FUNCTION__,                         \
                     __LINE__);                            \
            exit (1);                                      \
        } while (0)

#endif /* !_UTIL_OOM_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
