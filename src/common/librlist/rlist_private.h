/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_SCHED_RLIST_PRIVATE_H
#define HAVE_SCHED_RLIST_PRIVATE_H 1

int rlist_add_rnode (struct rlist *rl, struct rnode *n);

typedef struct rnode * (*rnode_copy_f) (const struct rnode *, void *arg);

struct rlist *rlist_copy_internal (const struct rlist *orig,
                                   rnode_copy_f cpfn,
                                   void *arg);

#endif /* !HAVE_SCHED_RLIST_PRIVATE_H */
