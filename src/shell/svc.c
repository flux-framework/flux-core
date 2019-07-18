/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Register a service named "shell-<jobid>" on each shell and provide
 * helpers for registering request handlers for different "methods".
 *
 * Notes:
 * - Message handlers are not exposed.  They are automatically set up to
 *   allow FLUX_ROLE_USER access, started, and tied to flux_t for destruction.
 *
 * - Since request handlers can receive messages from any user, handlers
 *   should call shell_svc_allowed() to verify that sender is instance owner,
 *   or the shell user (job owner).
 *
 * - shell_svc_create () makes a synchronous RPC to register the service with
 *   the broker.
 *
 * - Services should not be used until after the shells exit the init barrier,
 *   to ensure service registration has completed.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <flux/core.h>

#include "task.h"
#include "io.h"
#include "shell.h"
#include "svc.h"

#define TOPIC_STRING_SIZE  128

struct shell_svc {
    flux_shell_t *shell;
    uid_t uid;      // effective uid of shell
    int *rank_table;// map shell rank to broker rank
};

static int lookup_rank (struct shell_svc *svc, int shell_rank, int *rank)
{
    if (shell_rank < 0 || shell_rank >= svc->shell->info->shell_size) {
        errno = EINVAL;
        return -1;
    }
    *rank = svc->rank_table[shell_rank];
    return 0;
}

static int build_topic (struct shell_svc *svc,
                        const char *method,
                        char *buf,
                        int len)
{
    if (snprintf (buf,
                  len,
                  "shell-%ju%s%s",
                  (uintmax_t)svc->shell->info->jobid,
                  method ? "." : "",
                  method ? method : "") >= len) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

flux_future_t *shell_svc_pack (struct shell_svc *svc,
                               const char *method,
                               int shell_rank,
                               int flags,
                               const char *fmt, ...)
{
    char topic[TOPIC_STRING_SIZE];
    flux_future_t *f;
    va_list ap;
    int rank;

    if (lookup_rank (svc, shell_rank, &rank) < 0)
        return NULL;
    if (build_topic (svc, method, topic, sizeof (topic)) < 0)
        return NULL;

    va_start (ap, fmt);
    f = flux_rpc_vpack (svc->shell->h, topic, rank, flags, fmt, ap);
    va_end (ap);

    return f;
}

int shell_svc_allowed (struct shell_svc *svc, const flux_msg_t *msg)
{
    uint32_t rolemask;
    uint32_t userid;

    if (flux_msg_get_rolemask (msg, &rolemask) < 0
            || flux_msg_get_userid (msg, &userid) < 0)
        return -1;
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != svc->uid) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int shell_svc_register (struct shell_svc *svc,
                        const char *method,
                        flux_msg_handler_f cb,
                        void *arg)
{
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *mh;
    flux_t *h = svc->shell->h;
    char topic[TOPIC_STRING_SIZE];

    if (build_topic (svc, method, topic, sizeof (topic)) < 0)
        return -1;
    match.topic_glob = topic;
    if (!(mh = flux_msg_handler_create (h, match, cb, arg)))
        return -1;
    if (flux_aux_set (h, NULL, mh, (flux_free_f)flux_msg_handler_destroy) < 0) {
        flux_msg_handler_destroy (mh);
        return -1;
    }
    flux_msg_handler_allow_rolemask (mh, FLUX_ROLE_USER);
    flux_msg_handler_start (mh);
    return 0;
}

void shell_svc_destroy (struct shell_svc *svc)
{
    if (svc) {
        int saved_errno = errno;
        free (svc->rank_table);
        free (svc);
        errno = saved_errno;
    }
}

struct shell_svc *shell_svc_create (flux_shell_t *shell)
{
    struct shell_svc *svc;
    struct rcalc_rankinfo ri;
    int shell_size = shell->info->shell_size;
    int i;

    if (!(svc = calloc (1, sizeof (*svc))))
        return NULL;
    svc->shell = shell;
    svc->uid = geteuid ();
    if (!(svc->rank_table = calloc (shell_size, sizeof (*svc->rank_table))))
        goto error;
    for (i = 0; i < shell_size; i++) {
        if (rcalc_get_nth (shell->info->rcalc, i, &ri) < 0)
            goto error;
        svc->rank_table[i] = ri.rank;
    }
    if (!shell->standalone) {
        flux_future_t *f;
        char name[TOPIC_STRING_SIZE];
        if (build_topic (svc, NULL, name, sizeof (name)) < 0)
            goto error;
        if (!(f = flux_service_register (shell->h, name)))
            goto error;
        if (flux_future_get (f, NULL) < 0) {
            flux_future_destroy (f);
            goto error;
        }
        flux_future_destroy (f);
    }
    return svc;
error:
    shell_svc_destroy (svc);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
