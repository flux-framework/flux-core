/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* exchange_tree.c - k-ary tree placement over a participant subset */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "src/common/libutil/kary.h"

#include "exchange_tree.h"

void exchange_tree_init (struct exchange_tree *tree,
                         int k,
                         const int *subset,
                         int n,
                         int rank)
{
    memset (tree, 0, sizeof (*tree));
    tree->size = n;
    tree->index = -1;
    tree->parent = -1;

    for (int i = 0; i < n; i++) {
        if (subset[i] == rank) {
            tree->index = i;
            break;
        }
    }
    if (tree->index < 0)
        return; // not a participant

    if (tree->index > 0) {
        uint32_t pidx = kary_parentof (k, tree->index);
        if (pidx != KARY_NONE)
            tree->parent = subset[pidx];
    }
    for (int i = 0; i < k; i++) {
        if (kary_childof (k, n, tree->index, i) != KARY_NONE)
            tree->nchildren++;
    }
}

/* vi: ts=4 sw=4 expandtab
 */
