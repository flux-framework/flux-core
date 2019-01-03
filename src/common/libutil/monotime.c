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
#include <time.h>
#include <stdbool.h>

#include "monotime.h"

static struct timespec ts_diff (struct timespec start, struct timespec end)
{
        struct timespec temp;
        if ((end.tv_nsec-start.tv_nsec)<0) {
                temp.tv_sec = end.tv_sec-start.tv_sec-1;
                temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
        } else {
                temp.tv_sec = end.tv_sec-start.tv_sec;
                temp.tv_nsec = end.tv_nsec-start.tv_nsec;
        }
        return temp;
}

double monotime_since (struct timespec t0)
{
    struct timespec ts, d;
    clock_gettime (CLOCK_MONOTONIC, &ts);

    d = ts_diff (t0, ts);

    return ((double) d.tv_sec * 1000 + (double) d.tv_nsec / 1000000);
}

void monotime (struct timespec *tp)
{
    clock_gettime (CLOCK_MONOTONIC, tp);
}

bool monotime_isset (struct timespec t)
{
    return (t.tv_sec || t.tv_nsec);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
