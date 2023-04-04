/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* pmi_exchange.c - sync local dict across shells
 *
 * Gather key-value dict from each shell to shell 0, then broadcast
 * the aggregate dict to all shells.
 *
 * Each shell calls pmi_exchange() with a json_t dictionary and
 * a callback.  Upon completion of the exchange, the callback is invoked.
 * The callback may access an updated json_t dictionary.
 *
 * A binary tree is computed across all shell ranks.
 * Gather aggregates hashes at each tree level, reducing the number
 * of messages that have to be handled by shell 0.
 * Broadcast fans out at each tree level, reducing the number of messages
 * that have to be sent by rank 0.
 *
 * N.B. This binary tree is created from thin air for algorithmic purposes.
 * Nodes that are peers in the ersatz tree may actually be multiple hops
 * apart on the Flux tree based overlay network at the broker level.
 */
#define FLUX_SHELL_PLUGIN_NAME "pmi-simple"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libutil/kary.h"

#include "info.h"
#include "internal.h"

#include "pmi_exchange.h"

#define DEFAULT_TREE_K 2

struct session {
    json_t *dict;               // container for gathered dictionary
    pmi_exchange_f cb;          // callback for exchange completion
    void *cb_arg;

    zlist_t *requests;          // pending requests from children
    flux_future_t *f;           // pending request to parent

    struct pmi_exchange *pex;
    unsigned int local:1;       // pmi_exchange() was called on this shell
    unsigned int has_error:1;   // an error occurred
};

struct pmi_exchange {
    flux_shell_t *shell;
    int size;
    int rank;
    uint32_t parent_rank;
    int child_count;

    struct session *session;
};

static void exchange_response_completion (flux_future_t *f, void *arg);

static void session_destroy (struct session *ses)
{
    if (ses) {
        int saved_errno = errno;
        if (ses->requests) {
            const flux_msg_t *msg;
            while ((msg = zlist_pop (ses->requests)))
                flux_msg_decref (msg);
            zlist_destroy (&ses->requests);
        }
        flux_future_destroy (ses->f);
        json_decref (ses->dict);
        free (ses);
        errno = saved_errno;
    }
}

static struct session *session_create (struct pmi_exchange *pex)
{
    struct session *ses;

    if (!(ses = calloc (1, sizeof (*ses))))
        return NULL;
    ses->pex = pex;
    if (!(ses->requests = zlist_new ()))
        goto nomem;
    if (!(ses->dict = json_object ()))
        goto nomem;
    return ses;
nomem:
    errno = ENOMEM;
    session_destroy (ses);
    return NULL;
}

static void session_process (struct session *ses)
{
    struct pmi_exchange *pex = ses->pex;
    flux_t *h = ses->pex->shell->h;
    const flux_msg_t *msg;

    if (ses->has_error)
        goto done;

    /* Awaiting self or child input?
     */
    if (!ses->local || zlist_size (ses->requests) < pex->child_count)
        return;

    /* Send exchange request, if needed.
     */
    if (pex->rank > 0 && !ses->f) {
        flux_future_t *f;

        if (!(f = flux_shell_rpc_pack (pex->shell,
                                       "pmi-exchange",
                                       pex->parent_rank,
                                       0,
                                       "O",
                                       ses->dict))
                || flux_future_then (f,
                                     -1,
                                     exchange_response_completion,
                                     pex) < 0) {
            flux_future_destroy (f);
            shell_warn ("error sending pmi-exchange request");
            ses->has_error = 1;
            goto done;
        }
        ses->f = f;
    }

    /* Awaiting parent response?
     */
    if (ses->f && !flux_future_is_ready (ses->f))
        return;

    /* Send exchange response(s), if needed.
     */
    while ((msg = zlist_pop (ses->requests))) {
        if (flux_respond_pack (h, msg, "O", ses->dict) < 0) {
            shell_warn ("error responding to pmi-exchange request");
            flux_msg_decref (msg);
            ses->has_error = 1;
            goto done;
        }
        flux_msg_decref (msg);
    }
done:
    ses->cb (pex, ses->cb_arg);
    session_destroy (ses);
    pex->session = NULL;
}

/* PMI implementation on parent has responded to pmi-exchange request.
 */
static void exchange_response_completion (flux_future_t *f, void *arg)
{
    struct pmi_exchange *pex = arg;
    json_t *dict;

    if (flux_rpc_get_unpack (f, "o", &dict) < 0) {
        shell_warn ("pmi-exchange request: %s", future_strerror (f, errno));
        pex->session->has_error = 1;
        goto done;
    }
    if (json_object_update (pex->session->dict, dict) < 0) {
        shell_warn ("pmi-exchange response handling failed to update dict");
        pex->session->has_error = 1;
        goto done;
    }
done:
    session_process (pex->session);
}

/* PMI implementation on child sent a pmi-exchange request
 */
static void exchange_request_cb (flux_t *h,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    struct pmi_exchange *pex = arg;
    json_t *dict;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "o", &dict) < 0)
        goto error;
    if (!pex->session) {
        if (!(pex->session = session_create (pex)))
            goto error;
    }
    if (zlist_size (pex->session->requests) == pex->child_count) {
        errstr = "pmi-exchange received too many child requests";
        errno = EINPROGRESS;
        goto error;
    }
    if (json_object_update (pex->session->dict, dict) < 0) {
        errstr = "pmi-exchange request failed to update dict";
        goto nomem;
    }
    if (zlist_append (pex->session->requests,
                      (void *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        errstr = "pmi-exchange request failed to save pending request";
        goto nomem;
    }
    session_process (pex->session);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        shell_warn ("error responding to pmi-exchange request: %s",
                    flux_strerror (errno));
}

/* PMI implementation on _this_ shell is ready to exchange.
 */
int pmi_exchange (struct pmi_exchange *pex,
                  json_t *dict,
                  pmi_exchange_f cb,
                  void *arg)
{
    if (!pex->session) {
        if (!(pex->session = session_create (pex)))
            return -1;
    }
    if (pex->session->local) {
        errno = EINPROGRESS;
        return -1;
    }
    pex->session->cb = cb;
    pex->session->cb_arg = arg;
    pex->session->local = 1;
    if (json_object_update (pex->session->dict, dict) < 0) {
        errno = ENOMEM;
        return -1;
    }
    session_process (pex->session);
    return 0;
}

/* Helper for pmi_exchange_create() - calculate the number of children of
 * 'rank' in a 'size' tree of degree 'k'.
 */
static int child_count (int k, int rank, int size)
{
    int i;
    int count = 0;

    for (i = 0; i < k; i++) {
        if (kary_childof (k, size, rank, i) != KARY_NONE)
            count++;
    }
    return count;
}

struct pmi_exchange *pmi_exchange_create (flux_shell_t *shell, int k)
{
    struct pmi_exchange *pex;

    if (!(pex = calloc (1, sizeof (*pex))))
        return NULL;
    if (k <= 0)
        k = DEFAULT_TREE_K;
    else if (k > shell->info->shell_size) {
        k = shell->info->shell_size;
        if (shell->info->shell_rank == 0)
            shell_warn ("requested exchange fanout too large, using k=%d", k);
    }
    else {
        if (shell->info->shell_rank == 0)
            shell_warn ("using k=%d", k);
    }
    pex->shell = shell;
    pex->size = shell->info->shell_size;
    pex->rank = shell->info->shell_rank;
    pex->parent_rank = kary_parentof (k, pex->rank);
    pex->child_count = child_count (k, pex->rank, pex->size);

    if (flux_shell_service_register (shell,
                                     "pmi-exchange",
                                     exchange_request_cb,
                                     pex) < 0)
        goto error;
    return pex;
error:
    pmi_exchange_destroy (pex);
    return NULL;
}

void pmi_exchange_destroy (struct pmi_exchange *pex)
{
    if (pex) {
        int saved_errno = errno;
        session_destroy (pex->session);
        free (pex);
        errno = saved_errno;
    }
}

bool pmi_exchange_has_error (struct pmi_exchange *pex)
{
    return pex->session->has_error ? true : false;
}

json_t *pmi_exchange_get_dict (struct pmi_exchange *pex)
{
    return pex->session->dict;
}

/* vi: ts=4 sw=4 expandtab
 */
