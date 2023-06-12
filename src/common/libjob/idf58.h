/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _IDF58_H
#define _IDF58_H

#include <flux/core.h>

/*  Convenience function to convert a flux_jobid_t to F58 encoding
 *  If the encode fails (unlikely), then the decimal encoding is returned.
 */
static inline const char *idf58 (flux_jobid_t id)
{
    static __thread char buf[21];
    if (flux_job_id_encode (id, "f58", buf, sizeof (buf)) < 0) {
        /* 64bit integer is guaranteed to fit in 21 bytes
         * floor(log(2^64-1)/log(1)) + 1 = 20
         */
        (void) sprintf (buf, "%ju", (uintmax_t) id);
    }
    return buf;
}

#endif /* !_IDF58_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
