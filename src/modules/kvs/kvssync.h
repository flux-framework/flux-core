/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_KVSSYNC_H
#    define _FLUX_KVS_KVSSYNC_H

#    include <stdbool.h>
#    include <flux/core.h>

#    include "kvsroot.h"

typedef bool (*kvssync_test_msg_f) (const flux_msg_t *msg, void *arg);

/* add a kvssync structure to the kvsroot synclist */
int kvssync_add (struct kvsroot *root,
                 flux_msg_handler_f cb,
                 flux_t *h,
                 flux_msg_handler_t *mh,
                 const flux_msg_t *msg,
                 void *arg,
                 int seq);

/* if a root sequence number has gone past a sync sequence number,
 * call the callback.  If 'all' is true, run callback on all synclist
 * regardless.
 */
void kvssync_process (struct kvsroot *root, bool all);

/* remove message on synclist that meet 'cmp' conditions */
int kvssync_remove_msg (struct kvsroot *root,
                        kvssync_test_msg_f cmp,
                        void *arg);

#endif /* !_FLUX_KVS_KVSROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
