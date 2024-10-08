/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_WAIT_VERSION_H
#define _FLUX_KVS_WAIT_VERSION_H

#include <stdbool.h>
#include <flux/core.h>

#include "kvsroot.h"

typedef bool (*kvs_wait_version_test_msg_f)(const flux_msg_t *msg, void *arg);

/* add a kvs_wait_version structure to the kvsroot synclist */
int kvs_wait_version_add (struct kvsroot *root,
                          flux_msg_handler_f cb,
                          flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg,
                          int seq);

/* if a root sequence number has gone past a sequence number, call the
 * callback.  If 'all' is true, run callback on all wait_version_list
 * regardless.
 */
void kvs_wait_version_process (struct kvsroot *root, bool all);

/* remove message on wait_version_list that meet 'cmp' conditions */
int kvs_wait_version_remove_msg (struct kvsroot *root,
                                 kvs_wait_version_test_msg_f cmp,
                                 void *arg);

#endif /* !_FLUX_KVS_WAIT_VERSION_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
