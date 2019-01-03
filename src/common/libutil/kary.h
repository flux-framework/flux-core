/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_KARY_H
#define _UTIL_KARY_H

#include <stdint.h>

/* k-ary tree utils
 *
 * Assumption: trees are rooted at rank 0 and are "complete"
 * c.f. http://cs.lmu.edu/~ray/notes/orderedtrees/
 */

#define KARY_NONE   (~(uint32_t)0)

/* Return the parent of i or KARY_NONE if i has no parent.
 */
uint32_t kary_parentof (int k, uint32_t i);

/* Return the jth child of i or KARY_NONE if i has no such child.
 */
uint32_t kary_childof (int k, uint32_t size, uint32_t i, int j);

/* Return the level of i (root is level 0).
 */
int kary_levelof (int k, uint32_t i);

/* Count the number of descendants of i.
 */
int kary_sum_descendants (int k, uint32_t size, uint32_t i);

/* Return the parent of src if src is a descendant of dst,
 * KARY_NONE if it is not a descendant.
 */
uint32_t kary_parent_route (int k, uint32_t size, uint32_t src, uint32_t dst);

/* Return a child of src if dst is a descendant of that child,
 * KARY_NONE if dst is a descendant of no child of src.
 */
uint32_t kary_child_route (int k, uint32_t size, uint32_t src, uint32_t dst);


#endif /* !_UTIL_KARY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
