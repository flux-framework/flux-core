/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
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
        n = k*(i + 1) - (k - 2) + j - 1;
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
