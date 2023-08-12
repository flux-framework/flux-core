/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_H
#define _BROKER_H

#include <flux/optparse.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

struct broker {
    flux_t *h;
    flux_reactor_t *reactor;
    optparse_t *opts;

    struct overlay *overlay;
    uint32_t rank;
    uint32_t size;

    bool online;

    double starttime;

    struct broker_attr *attrs;
    struct flux_msg_cred cred;  /* instance owner */

    struct modhash *modhash;

    int verbose;
    int event_recv_seq;
    zlist_t *sigwatchers;
    struct service_switch *services;
    struct brokercfg *config;
    double heartbeat_rate;
    struct subhash *sub;        /* subscriptions for internal services */
    struct content_cache *cache;
    struct publisher *publisher;
    struct groups *groups;

    struct runat *runat;
    struct state_machine *state_machine;
    struct shutdown *shutdown;

    char *init_shell_cmd;
    size_t init_shell_cmd_len;

    bool sd_notify;

    int exit_rc;
};

typedef struct broker broker_ctx_t;

#endif /* !_BROKER_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
