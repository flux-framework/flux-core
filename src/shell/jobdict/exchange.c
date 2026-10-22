/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* exchange.c - sync a dict across shells (jobdict's gather+broadcast engine)
 *
 * Each shell calls exchange_enter() with a json_t dictionary and an exit
 * callback.  The dictionaries are gathered to the lowest participant over a
 * k-ary tree, merged at each level, then the aggregate dictionary is
 * broadcast back down the tree.  When the result is available on a shell,
 * its exit callback is invoked and may read the result with
 * exchange_get_result().
 *
 * A k-ary tree is computed across the participating shell ranks.  Gather
 * aggregates at each tree level, reducing the number of messages the root
 * must handle; broadcast fans out at each level, reducing the number of
 * messages the root must send.
 *
 * N.B. This tree is created from thin air for algorithmic purposes.  Nodes
 * that are peers in the ersatz tree may actually be multiple hops apart on the
 * Flux tree based overlay network at the broker level.
 */
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "exchange.h"
#include "exchange_tree.h"

#define DEFAULT_TREE_K 2

/* Log under the exchange's name rather than a fixed plugin name, since this
 * component is shared by multiple dictionaries.
 */
#define xcg_warn(xcg, ...) \
    flux_shell_log ((xcg)->name, FLUX_SHELL_WARN, __FILE__, __LINE__, \
                    __VA_ARGS__)
#define xcg_debug(xcg, ...) \
    flux_shell_log ((xcg)->name, FLUX_SHELL_DEBUG, __FILE__, __LINE__, \
                    __VA_ARGS__)

struct exchange_session {
    char *tag;                  // session key (identifies concurrent sessions)
    json_t *data_in;            // gathered dict from self + subtree
    json_t *result;             // aggregate dict (broadcast down the tree)
    exchange_exit_f exit_cb;    // callback for exchange completion
    void *exit_cb_arg;

    int index;                  // this shell's index in the participant set
    int parent;                 // parent shell rank, or -1 if root of subtree
    int nchildren;              // number of children in the participant tree

    zlist_t *requests;          // pending requests from children
    flux_future_t *f;           // pending request to parent

    struct exchange *xcg;
    unsigned int local:1;       // exchange_enter() was called on this shell
    unsigned int has_error:1;   // an error occurred
};

struct exchange {
    flux_shell_t *shell;
    flux_t *h;
    char *name;                 // identifies this handle in inter-shell RPCs
    int size;
    int rank;
    int k;                      // resolved tree fanout

    zhashx_t *sessions;         // struct exchange_session hashed by tag
};

static void response_completion (flux_future_t *f, void *arg);

static void session_destroy (struct exchange_session *ses)
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
        json_decref (ses->data_in);
        json_decref (ses->result);
        free (ses->tag);
        free (ses);
        errno = saved_errno;
    }
}

/* zhashx_destructor_fn footprint */
static void session_destructor (void **item)
{
    if (item) {
        session_destroy (*item);
        *item = NULL;
    }
}

static struct exchange_session *session_create (struct exchange *xcg,
                                                  const char *tag)
{
    struct exchange_session *ses;

    if (!(ses = calloc (1, sizeof (*ses))))
        return NULL;
    ses->xcg = xcg;
    ses->index = -1;
    ses->parent = -1;
    if (!(ses->tag = strdup (tag)))
        goto error;
    if (!(ses->requests = zlist_new ()))
        goto nomem;
    if (!(ses->data_in = json_object ()))
        goto nomem;
    if (zhashx_insert (xcg->sessions, tag, ses) < 0) {
        errno = EEXIST;
        goto error;
    }
    return ses;
nomem:
    errno = ENOMEM;
error:
    session_destroy (ses);
    return NULL;
}

/* Look up an existing session by tag, or create one on demand.
 */
static struct exchange_session *session_lookup (struct exchange *xcg,
                                                  const char *tag)
{
    struct exchange_session *ses;

    if (!(ses = zhashx_lookup (xcg->sessions, tag)))
        ses = session_create (xcg, tag);
    return ses;
}

/* Fail any children still awaiting a response so the error propagates
 * down the tree rather than leaving them blocked.
 */
static void session_fail_children (struct exchange_session *ses)
{
    struct exchange *xcg = ses->xcg;
    const flux_msg_t *msg;

    while ((msg = zlist_pop (ses->requests))) {
        if (flux_respond_error (xcg->h, msg, EIO, "exchange failed") < 0)
            xcg_warn (xcg, "error responding to request");
        flux_msg_decref (msg);
    }
}

static void session_process (struct exchange_session *ses)
{
    struct exchange *xcg = ses->xcg;
    const flux_msg_t *msg;

    if (ses->has_error)
        goto done;

    /* Awaiting self or child input?
     */
    if (!ses->local || zlist_size (ses->requests) < ses->nchildren)
        return;

    /* Send gather request to parent, if needed.
     */
    if (ses->parent >= 0 && !ses->f) {
        flux_future_t *f;

        if (!(f = flux_shell_rpc_pack (xcg->shell,
                                       EXCHANGE_SERVICE,
                                       ses->parent,
                                       0,
                                       "{s:s s:s s:O}",
                                       "name", xcg->name,
                                       "tag", ses->tag,
                                       "data", ses->data_in))
                || flux_future_then (f,
                                     -1,
                                     response_completion,
                                     ses) < 0) {
            flux_future_destroy (f);
            xcg_warn (xcg, "error sending request");
            ses->has_error = 1;
            goto done;
        }
        ses->f = f;
    }

    /* Awaiting parent response?
     */
    if (ses->f && !flux_future_is_ready (ses->f))
        return;

    /* The root of the participant tree (index 0) holds the final result.
     */
    if (ses->index == 0)
        ses->result = json_incref (ses->data_in);

    /* Broadcast result to children, if any.
     */
    while ((msg = zlist_pop (ses->requests))) {
        if (flux_respond_pack (xcg->h, msg, "{s:O}", "data", ses->result) < 0) {
            xcg_warn (xcg, "error responding to request");
            flux_msg_decref (msg);
            ses->has_error = 1;
            goto done;
        }
        flux_msg_decref (msg);
    }
done:
    session_fail_children (ses);
    /* The exit callback runs while the session is still in the table so it
     * may read the result with exchange_get_result(tag); the session is
     * removed (and freed) once it returns.
     */
    ses->exit_cb (xcg, ses->tag, ses->exit_cb_arg);
    zhashx_delete (xcg->sessions, ses->tag);
}

/* Parent shell has responded to our gather request with the aggregate result.
 */
static void response_completion (flux_future_t *f, void *arg)
{
    struct exchange_session *ses = arg;
    struct exchange *xcg = ses->xcg;
    json_t *result;

    /* Unpack a borrowed reference and incref on success only, so that a
     * malformed response cannot leave a reference behind.
     */
    if (flux_rpc_get_unpack (f, "{s:o}", "data", &result) < 0) {
        xcg_warn (xcg, "request: %s", future_strerror (f, errno));
        ses->has_error = 1;
    }
    else
        ses->result = json_incref (result);
    session_process (ses);
}

/* Child shell sent a gather request.
 */
void exchange_request (struct exchange *xcg, const flux_msg_t *msg)
{
    struct exchange_session *ses;
    const char *tag;
    json_t *data;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s s:o}", "tag", &tag, "data", &data)
        < 0)
        goto error;
    if (!(ses = session_lookup (xcg, tag)))
        goto error;
    /* The exact child count is known only once this shell has entered (the
     * participant set is supplied then).  Before that, no node can have more
     * than k children, so bound queued requests by the fanout.
     */
    if (zlist_size (ses->requests) >= (ses->local ? ses->nchildren : xcg->k)) {
        errstr = "exchange received too many child requests";
        errno = EINPROGRESS;
        goto error;
    }
    if (!json_is_object (data)
        || json_object_update (ses->data_in, data) < 0) {
        errstr = "exchange request failed to merge dict";
        errno = EPROTO;
        goto error;
    }
    if (zlist_append (ses->requests, (void *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        errstr = "exchange request failed to save pending request";
        errno = ENOMEM;
        goto error;
    }
    session_process (ses);
    return;
error:
    if (flux_respond_error (xcg->h, msg, errno, errstr) < 0)
        xcg_warn (xcg,
                  "error responding to request: %s",
                  flux_strerror (errno));
}

/* Build a sorted, duplicate-free array of participant shell ranks.  If
 * 'participants' is NULL the participant set defaults to all shells
 * (0..size-1).  Caller frees *subsetp.  Returns 0 on success, -1 on failure.
 */
static int build_subset (struct exchange *xcg,
                         const struct idset *participants,
                         int **subsetp,
                         int *np)
{
    int *subset;
    int n;

    if (participants) {
        unsigned int id;
        int i = 0;

        n = idset_count (participants);
        if (!(subset = calloc (n > 0 ? n : 1, sizeof (int))))
            return -1;
        id = idset_first (participants);
        while (id != IDSET_INVALID_ID) {
            subset[i++] = id;
            id = idset_next (participants, id);
        }
    }
    else {
        n = xcg->size;
        if (!(subset = calloc (n, sizeof (int))))
            return -1;
        for (int i = 0; i < n; i++)
            subset[i] = i;
    }
    *subsetp = subset;
    *np = n;
    return 0;
}

int exchange_enter (struct exchange *xcg,
                    const char *tag,
                    const struct idset *participants,
                    json_t *dict,
                    exchange_exit_f exit_cb,
                    void *arg)
{
    struct exchange_session *ses;
    struct exchange_tree tree;
    int *subset;
    int n;
    int saved_errno;

    if (!xcg || !tag || !exit_cb) {
        errno = EINVAL;
        return -1;
    }
    if (build_subset (xcg, participants, &subset, &n) < 0)
        return -1;
    exchange_tree_init (&tree, xcg->k, subset, n, xcg->rank);
    free (subset);
    if (tree.index < 0) {
        /* This shell is not a member of the participant set. */
        errno = EINVAL;
        return -1;
    }
    if (!(ses = session_lookup (xcg, tag)))
        return -1;
    if (ses->local) {
        errno = EINPROGRESS;
        return -1;
    }
    /* Requests queued before entry were bounded by the fanout; now that the
     * child count is known, reject the session if more than that arrived.
     */
    if (zlist_size (ses->requests) > tree.nchildren) {
        xcg_warn (xcg,
                  "%zu child requests queued for %s but %d expected",
                  zlist_size (ses->requests),
                  tag,
                  tree.nchildren);
        errno = EPROTO;
        goto error;
    }
    if (dict && json_object_update (ses->data_in, dict) < 0) {
        errno = ENOMEM;
        goto error;
    }
    ses->index = tree.index;
    ses->parent = tree.parent;
    ses->nchildren = tree.nchildren;
    ses->exit_cb = exit_cb;
    ses->exit_cb_arg = arg;
    ses->local = 1;
    session_process (ses);
    return 0;
error:
    /* The session cannot proceed, so fail any children already queued on it
     * and discard it rather than leaving it half-initialized in the table.
     */
    saved_errno = errno;
    session_fail_children (ses);
    zhashx_delete (xcg->sessions, tag);
    errno = saved_errno;
    return -1;
}

struct exchange *exchange_create (flux_shell_t *shell,
                                  const char *name,
                                  int k)
{
    struct exchange *xcg;

    if (!shell || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(xcg = calloc (1, sizeof (*xcg))))
        return NULL;
    xcg->shell = shell;
    if (!(xcg->h = flux_shell_get_flux (shell)))
        goto error;
    if (!(xcg->name = strdup (name)))
        goto error;
    if (!(xcg->sessions = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (xcg->sessions, session_destructor);
    if (flux_shell_info_unpack (shell,
                                "{s:i s:i}",
                                "size", &xcg->size,
                                "rank", &xcg->rank) < 0)
        goto error;
    if (k <= 0)
        k = DEFAULT_TREE_K;
    else if (k > xcg->size) {
        k = xcg->size;
        if (xcg->rank == 0)
            xcg_warn (xcg, "requested fanout too large, using k=%d", k);
    }
    if (xcg->rank == 0)
        xcg_debug (xcg, "using k=%d", k);
    xcg->k = k;
    return xcg;
error:
    exchange_destroy (xcg);
    return NULL;
}

void exchange_destroy (struct exchange *xcg)
{
    if (xcg) {
        int saved_errno = errno;
        zhashx_destroy (&xcg->sessions);
        free (xcg->name);
        free (xcg);
        errno = saved_errno;
    }
}

bool exchange_has_error (struct exchange *xcg, const char *tag)
{
    struct exchange_session *ses = zhashx_lookup (xcg->sessions, tag);
    return (ses && ses->has_error) ? true : false;
}

json_t *exchange_get_result (struct exchange *xcg, const char *tag)
{
    struct exchange_session *ses = zhashx_lookup (xcg->sessions, tag);
    return ses ? ses->result : NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
