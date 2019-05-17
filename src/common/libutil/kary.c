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
#    include "config.h"
#endif

#include "kary.h"

uint32_t kary_parentof (int k, uint32_t i)
{
    if (i == 0 || k <= 0)
        return KARY_NONE;
    if (k == 1)
        return i - 1;
    return (k + (i + 1) - 2) / k - 1;
}

uint32_t kary_childof (int k, uint32_t size, uint32_t i, int j)
{
    uint32_t n;

    if (k > 0 && j >= 0 && j < k) {
        n = k * (i + 1) - (k - 2) + j - 1;
        if (n < size)
            return n;
    }
    return KARY_NONE;
}

int kary_levelof (int k, uint32_t i)
{
    int32_t n = kary_parentof (k, i);

    if (n == KARY_NONE)
        return 0;
    return 1 + kary_levelof (k, n);
}

int kary_sum_descendants (int k, uint32_t size, uint32_t i)
{
    int sum = 0;
    int n, j;

    for (j = 0; j < k; j++) {
        n = kary_childof (k, size, i, j);
        if (n != KARY_NONE)
            sum += 1 + kary_sum_descendants (k, size, n);
    }
    return sum;
}

uint32_t kary_parent_route (int k, uint32_t size, uint32_t src, uint32_t dst)
{
    uint32_t n, gw;

    if (k > 0 && src != dst && dst < size && src < size) {
        n = gw = kary_parentof (k, src);
        while (n != KARY_NONE) {
            if (n == dst)
                return gw;
            n = kary_parentof (k, n);
        }
    }
    return KARY_NONE;
}

uint32_t kary_child_route (int k, uint32_t size, uint32_t src, uint32_t dst)
{
    uint32_t gw;
    int j;

    if (k > 0 && src != dst && dst < size && src < size) {
        gw = kary_parentof (k, dst);
        if (gw == src)
            return dst;
        while (gw != KARY_NONE) {
            for (j = 0; j < k; j++) {
                if (gw == kary_childof (k, size, src, j))
                    return gw;
            }
            gw = kary_parentof (k, gw);
        }
    }
    return KARY_NONE;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
