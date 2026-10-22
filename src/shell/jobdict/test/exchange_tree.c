/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include "src/common/libtap/tap.h"
#include "src/shell/jobdict/exchange_tree.h"

/* Check placement of 'rank' in a k-ary tree over 'subset' (length n). */
static void check (int k,
                   const int *subset,
                   int n,
                   int rank,
                   int exp_index,
                   int exp_parent,
                   int exp_nchildren)
{
    struct exchange_tree tree;

    exchange_tree_init (&tree, k, subset, n, rank);
    ok (tree.index == exp_index
        && tree.parent == exp_parent
        && tree.nchildren == exp_nchildren
        && tree.size == n,
        "k=%d n=%d rank=%d: index=%d parent=%d nchildren=%d",
        k, n, rank, tree.index, tree.parent, tree.nchildren);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    /* Full binary tree over all 8 shells (subset = identity).
     * children of index i are 2i+1, 2i+2; parent is (i-1)/2.
     */
    int full[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    check (2, full, 8, 0, 0, -1, 2); // root
    check (2, full, 8, 1, 1, 0, 2);
    check (2, full, 8, 2, 2, 0, 2);
    check (2, full, 8, 3, 3, 1, 1); // child 7 only
    check (2, full, 8, 4, 4, 1, 0); // leaf
    check (2, full, 8, 7, 7, 3, 0); // leaf

    /* Ternary tree over all 8 shells: children of i are 3i+1,3i+2,3i+3. */
    check (3, full, 8, 0, 0, -1, 3);
    check (3, full, 8, 1, 1, 0, 3); // children 4,5,6
    check (3, full, 8, 2, 2, 0, 1); // child 7 only
    check (3, full, 8, 7, 7, 2, 0); // leaf

    /* Subset tree: participants are shell ranks 2,5,7,9 (sorted).
     * Index space 0..3 maps to those ranks; parents map back to ranks.
     */
    int sub[] = { 2, 5, 7, 9 };
    check (2, sub, 4, 2, 0, -1, 2); // root (index 0), children idx 1,2
    check (2, sub, 4, 5, 1, 2, 1);  // parent rank 2, child idx 3
    check (2, sub, 4, 7, 2, 2, 0);  // parent rank 2, leaf
    check (2, sub, 4, 9, 3, 5, 0);  // parent rank 5 (idx 1), leaf

    /* A shell not in the participant set has no placement. */
    check (2, sub, 4, 4, -1, -1, 0);
    check (2, sub, 4, 0, -1, -1, 0);

    /* Single-participant subset: lone root, no parent, no children. */
    int one[] = { 3 };
    check (2, one, 1, 3, 0, -1, 0);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
