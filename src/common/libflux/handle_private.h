/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_HANDLE_PRIVATE_H
#define _FLUX_CORE_HANDLE_PRIVATE_H

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif

#include "src/common/libutil/aux.h"
#include "src/common/librouter/rpc_track.h"
#include "msg_deque.h"
#include "connector.h"
#include "tagpool.h"
#include "handle.h"

#if HAVE_CALIPER
struct profiling_context {
    int initialized;
    cali_id_t msg_type;
    cali_id_t msg_seq;
    cali_id_t msg_topic;
    cali_id_t msg_sender;
    cali_id_t msg_rpc;
    cali_id_t msg_rpc_nodeid;
    cali_id_t msg_rpc_resp_expected;
    cali_id_t msg_action;
    cali_id_t msg_match_type;
    cali_id_t msg_match_tag;
    cali_id_t msg_match_glob;
};
#endif

struct flux_handle {
    flux_t          *parent; // if FLUX_O_CLONE, my parent
    struct aux_item *aux;
    int             usecount;
    int             flags;

    const struct flux_handle_ops *ops;
    void            *impl;
    void            *dso;
    struct msg_deque *queue;
    int             pollfd;

    struct tagpool  *tagpool;
    flux_msgcounters_t msgcounters;
    flux_comms_error_f comms_error_cb;
    void            *comms_error_arg;
    bool            comms_error_in_progress;
    bool            destroy_in_progress;
#if HAVE_CALIPER
    struct profiling_context prof;
#endif
    struct rpc_track *tracker;
};

static inline flux_t *lookup_clone_ancestor (flux_t *h)
{
    while (h && (h->flags & FLUX_O_CLONE))
        h = h->parent;
    return h;
}

#endif /* !_FLUX_CORE_HANDLE_PRIVATE_H */

// vi:ts=4 sw=4 expandtab
