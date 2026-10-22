/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_JOBDICT_EXCHANGE_TREE_H
#define SHELL_JOBDICT_EXCHANGE_TREE_H

/* Placement of one shell rank within a k-ary tree built over a participant
 * set of shell ranks.  The tree is rooted at the first (lowest) participant
 * and the parent/child relationships are computed in the index space of the
 * sorted participant set, then mapped back to shell ranks.  Only the parent
 * shell rank (where this rank pushes its gathered data) and the child count
 * (how many child dictionaries to await) are needed to drive the exchange;
 * children push to their parent, so their ranks are not required here.
 */
struct exchange_tree {
    int index;      // this rank's index in the participant set, -1 if absent
    int size;       // number of participants
    int parent;     // parent shell rank, or -1 if root or not a participant
    int nchildren;  // number of children of this rank in the tree
};

/* Initialize 'tree' for shell rank 'rank' given tree fanout 'k' and the
 * sorted, duplicate-free participant shell-rank array 'subset' of length 'n'.
 * If 'rank' is not a participant, tree->index and tree->parent are -1 and
 * tree->nchildren is 0.  The root participant (index 0) has parent -1.
 */
void exchange_tree_init (struct exchange_tree *tree,
                         int k,
                         const int *subset,
                         int n,
                         int rank);

#endif /* !SHELL_JOBDICT_EXCHANGE_TREE_H */

/* vi: ts=4 sw=4 expandtab
 */
