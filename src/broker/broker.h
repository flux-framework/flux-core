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

struct broker {
    flux_t *h;
    flux_reactor_t *reactor;

    struct overlay *overlay;
    uint32_t rank;
    uint32_t size;

    struct broker_attr *attrs;
    struct flux_msg_cred cred;  /* instance owner */

    struct modhash *modhash;

    bool verbose;
    int event_recv_seq;
    zlist_t *sigwatchers;
    struct service_switch *services;
    struct heartbeat *heartbeat;
    struct brokercfg *config;
    const char *config_path;
    struct shutdown *shutdown;
    double shutdown_grace;
    double heartbeat_rate;
    int sec_typemask;
    zlist_t *subscriptions;     /* subscripts for internal services */
    struct content_cache *cache;
    struct publisher *publisher;
    int tbon_k;

    struct hello *hello;
    struct runat *runat;
    broker_state_t state;

    char *init_shell_cmd;
    size_t init_shell_cmd_len;

    int exit_rc;
};

typedef struct broker broker_ctx_t;

#endif /* !_BROKER_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
