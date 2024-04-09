/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* groups.c - broker groups
 *
 * Track broker rank membership in multiple named groups.
 * Each broker tracks membership for its TBON subtree, with membership
 * for the full instance available at rank 0.
 * Membership is updated through JOIN and LEAVE requests.
 * An operation (join, leave, get) on an unknown group triggers its creation.
 * Groups are is never removed.
 *
 * N.B. JOIN and LEAVE requests set/clear the broker rank that processed
 * the request, therefore these requests must be sent to FLUX_NODEID_ANY
 * so that they are processed on the same broker as the requestor.
 *
 * If a disconnect notification is received, a LEAVE is automatically
 * generated for all groups that the disconnecting UUID has joined.
 * Similarly, if the overlay subsystem notifies us that a peer subtree has
 * become "lost", LEAVEs are automatically generated for all groups that
 * the subtree ranks belong to.
 *
 * Optimization: collect contemporaneous JOIN/LEAVE requests at each
 * rank for a short time before applying them and sending them upstream.
 * During that time, JOINs/LEAVEs of the same key may be combined.
 *
 * broker.online use case:
 * Groups are used for instance quorum detection.  The state machine calls
 * groups.join broker.online in the QUORUM state on all ranks.  Rank 0 calls
 * groups.get broker.online which notifies the broker as membership evolves,
 * and when the quorum condition is satisfied, the state transitions
 * to RUN.  The 'broker.online' group is also monitored by the resource module
 * so that it can inform the scheduler as execution targets go up/down.
 *
 * broker.torpid use case:
 * A broker.torpid group is maintained by the broker overlay (see overlay.c).
 * The resource module also monitors broker.torpid and drains torpid nodes.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <jansson.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "overlay.h"
#include "groups.h"

static const double batch_timeout = 0.1;

/* N.B. only one client can join a group per broker.  That client
 * join request is cached in group->join_request so that when the client
 * disconnects, we can identify its groups and force it to leave.
 */
struct group {
    char *name; // used directly as zhashx key
    struct idset *members;
    const flux_msg_t *join_request;
    struct flux_msglist *watchers;
};

struct groups {
    struct broker *ctx;
    flux_msg_handler_t **handlers;
    zhashx_t *groups;
    json_t *batch; // dict of arrays, keyed by group name
    flux_watcher_t *batch_timer;
    uint32_t rank;
    struct idset *self;
    struct idset *torpid; // current list of torpid peers at this broker rank
};

static void get_respond_all (struct groups *g, struct group *group);

static void group_destroy (struct group *group)
{
    if (group) {
        int saved_errno = errno;
        free (group->name);
        idset_destroy (group->members);
        flux_msg_decref (group->join_request);
        flux_msglist_destroy (group->watchers);
        free (group);
        errno = saved_errno;
    }
}

// zhashx_destructor_fn footprint
static void group_destructor (void **item)
{
    if (*item) {
        struct group *group = *item;
        group_destroy (group);
        *item = NULL;
    }
}

static struct group *group_create (const char *name)
{
    struct group *group;

    if (!(group = calloc (1, sizeof (*group)))
        || !(group->name = strdup (name))
        || !(group->watchers = flux_msglist_create ())
        || !(group->members = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        group_destroy (group);
        return NULL;
    }
    return group;
}

static struct group *group_lookup (struct groups *g,
                                   const char *name,
                                   bool create)
{
    struct group *group;

    if (!(group = zhashx_lookup (g->groups, name))) {
        if (!create) {
            errno = ENOENT;
            return NULL;
        }
        if (!(group = group_create (name)))
            return NULL;
        (void)zhashx_insert (g->groups, group->name, group);
    }
    return group;
}

/* Decode batch update object.
 * Caller must idset_destroy 'ranks' on success.
 * Returns 0 on success, -1 on failure with error set.
 */
static int update_decode (json_t *o, struct idset **ranksp, bool *set_flagp)
{
    const char *s;
    struct idset *ranks = NULL;
    int set_flag;

    if (json_unpack (o, "{s:s s:b}", "ranks", &s, "set", &set_flag) < 0
        || !(ranks = idset_decode (s))) {
        errno = EPROTO;
        return -1;
    }
    *ranksp = ranks;
    *set_flagp = set_flag ? true : false;
    return 0;
}

/* Encode batch update object.
 * Returns object on success, NULL on failure with errno set.
 */
static json_t *update_encode (struct idset *ranks, bool set_flag)
{
    char *s;
    json_t *o;

    if (!(s = idset_encode (ranks, IDSET_FLAG_RANGE)))
        return NULL;
    if (!(o = json_pack ("{s:s s:b}", "ranks", s, "set", set_flag))) {
        free (s);
        errno = ENOMEM;
        return NULL;
    }
    free (s);
    return o;
}

/* Apply one join/leave batch update to the local hash.
 */
static void batch_apply_one (struct groups *g,
                             struct group *group,
                             json_t *entry)
{
    struct idset *ranks;
    bool set_flag;
    int rc;

    if (update_decode (entry, &ranks, &set_flag) < 0) {
        flux_log_error (g->ctx->h,
                        "groups: error decoding batch update for group=%s",
                        group->name);
        return;
    }
    if (set_flag)
        rc = idset_add (group->members, ranks);
    else
        rc = idset_subtract (group->members, ranks);
    if (rc < 0) {
        flux_log_error (g->ctx->h,
                        "groups: error applying batch update for group=%s",
                        group->name);
    }
    idset_destroy (ranks);
}

/* Apply all batch updates to the local hash.
 * On rank 0, respond to any relevant groups.get requests.
 */
static void batch_apply (struct groups *g)
{
    const char *name;
    json_t *a;
    struct group *group;
    size_t index;
    json_t *entry;

    json_object_foreach (g->batch, name, a) {
        if (!(group = group_lookup (g, name, true))) {
            flux_log_error (g->ctx->h,
                "groups: error creating group during batch update for group=%s",
                name);
            continue;
        }
        json_array_foreach (a, index, entry) {
            batch_apply_one (g, group, entry);
        }
        get_respond_all (g, group);
    }
    json_object_clear (g->batch);
    flux_watcher_stop (g->batch_timer);
}

/* Add a batch update object to the batch queue, and activate timer if
 * queue was initially empty.  Queued updates are applied on timer expiration.
 * 'update' may be either an individual get/set operation, or an array of them.
 * Returns 0 if update object is accepted, -1 on failure with errno set.
 */
static int batch_append (struct groups *g, const char *name, json_t *update)
{
    size_t size = json_object_size (g->batch);
    json_t *a;
    int rc;

    if (!(a = json_object_get (g->batch, name))) {
        if (!(a = json_array ())
            || json_object_set_new (g->batch, name, a) < 0) {
            json_decref (a);
            goto nomem;
        }
    }
    if (json_is_array (update))
        rc = json_array_extend (a, update);
    else
        rc = json_array_append (a, update);
    if (rc < 0)
        goto nomem;
    if (size == 0) {
        flux_timer_watcher_reset (g->batch_timer, batch_timeout, 0.);
        flux_watcher_start (g->batch_timer);
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

/* Try to reduce like updates to a particular group into one update.
 * Returns the one update if successful, else NULL.  This is a "best effort"
 * operation, so NULL should not be treated as a fatal error.
 */
static json_t *batch_reduce_one (struct groups *g, json_t *a)
{
    size_t index;
    struct idset *ids = NULL;
    bool set_flag = false;
    json_t *update;
    json_t *new_update = NULL;

    if (json_array_size (a) < 2)
        return NULL;
    json_array_foreach (a, index, update) {
        struct idset *new_ids;
        bool new_set_flag;

        if (update_decode (update, &new_ids, &new_set_flag) < 0) {
            flux_log_error (g->ctx->h, "groups: reduce decode update failed");
            goto error;
        }
        if (index == 0) {
            ids = new_ids;
            set_flag = new_set_flag;
            continue;
        }
        if (new_set_flag != set_flag) {
            idset_destroy (new_ids);
            goto error;
        }
        if (idset_add (ids, new_ids) < 0) {
            flux_log_error (g->ctx->h, "groups: reduce idset update failed");
            idset_destroy (new_ids);
            goto error;
        }
        idset_destroy (new_ids);
    }
    if (!(new_update = update_encode (ids, set_flag)))
        flux_log_error (g->ctx->h, "groups: reduce encode update failed");
error:
    idset_destroy (ids);
    return new_update;
}

/* Try to reduce all keys in the current batch.  If a reduction is
 * successful, replace the current array of operations with the new one.
 * N.B. json_array_clear() + json_array_append() would be more direct,
 * but destructive if the append fails.
 */
static void batch_reduce (struct groups *g)
{
    const char *name;
    json_t *a;

    json_object_foreach (g->batch, name, a) {
        json_t *reduced;
        json_t *new_a = NULL;

        if (!(reduced = batch_reduce_one (g, a))
            || !(new_a = json_array ())
            || json_array_append (new_a, reduced) < 0
            || json_object_set (g->batch, name, new_a) < 0)
            goto next;
next:
        json_decref (new_a);
        json_decref (reduced);
    }
}

/* Apply all updates to local hash, and pass them upstream, if applicable.
 * This is called when the timer expires, and may also be called from the
 * disconnect and overlay loss handlers, which need to test group membership
 * before generating LEAVEs.  Stop the batch timer, if running.
 */
static void batch_flush (struct groups *g)
{
    batch_reduce (g);
    if (g->ctx->rank > 0) {
        flux_future_t *f;
        if (!(f = flux_rpc_pack (g->ctx->h,
                                 "groups.update",
                                 FLUX_NODEID_UPSTREAM,
                                 FLUX_RPC_NORESPONSE,
                                 "{s:O}",
                                 "update", g->batch)))
            flux_log_error (g->ctx->h, "error sending groups.update request");
        flux_future_destroy (f);
    }
    batch_apply (g);
    flux_watcher_stop (g->batch_timer);
}

/* Handle batch timeout.
 */
static void batch_timeout_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct groups *g = arg;
    batch_flush (g);
}

/* Enqueue updates from a downstream peer.  After the batch timer expires,
 * updates are applied to local hash and forwarded upstream.
 * This is an internal (broker to broker) RPC which requires no response.
 */
static void update_request_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct groups *g = arg;
    json_t *update;
    const char *name;
    json_t *a;

    if (flux_request_unpack (msg, NULL, "{s:o}", "update", &update) < 0) {
        flux_log_error (h, "error decoding groups.update request");
        return;
    }
    json_object_foreach (update, name, a) {
        if (batch_append (g, name, a) < 0)
            flux_log_error (h, "error enqueuing groups.update group=%s", name);
    }
}

/* Add this broker rank to a group.
 * Helper for groups.join RPC handler.
 */
static int groups_join (struct groups *g, const char *name)
{
    json_t *update;

    if (!(update = update_encode (g->self, true))
        || batch_append (g, name, update) < 0) {
        ERRNO_SAFE_WRAP (json_decref, update);
        return -1;
    }
    json_decref (update);
    return 0;
}

/* Remove this broker rank from a group.
 * Helper for groups.join RPC handler.
 */
static int groups_leave (struct groups *g, const char *name)
{
    json_t *update;

    if (!(update = update_encode (g->self, false))
        || batch_append (g, name, update) < 0) {
        ERRNO_SAFE_WRAP (json_decref, update);
        return -1;
    }
    json_decref (update);
    return 0;
}

/* Process client request to JOIN a group.
 * Create the group if needed, then add JOIN update to timed batch.
 * Respond immediately, as opposed to when the batch is processed.
 * The request is cached to support auto LEAVE upon client disconnect.
 */
static void join_request_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct groups *g = arg;
    const char *name;
    struct group *group;
    char errbuf[256];
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (!flux_msg_is_local (msg)) {
        errno = EPROTO;
        errmsg = "groups.join is restricted to the local broker";
        goto error;
    }
    if (!(group = group_lookup (g, name, true)))
        goto error;
    if (group->join_request) {
        snprintf (errbuf,
                  sizeof (errbuf),
                  "rank %lu is already a member of %s",
                  (unsigned long)g->ctx->rank,
                  name);
        errmsg = errbuf;
        errno = EEXIST;
        goto error;
    }
    if (groups_join (g, name) < 0)
        goto error;
    group->join_request = flux_msg_incref (msg);
    if (flux_respond (h, msg, NULL) < 0) {
        if (errno != ENOSYS)
            flux_log_error (h, "error responding to groups.leave request");
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0) {
        if (errno != ENOSYS)
            flux_log_error (h, "error responding to groups.leave request");
    }
}

/* A client wishes to LEAVE a group.
 * Drop cached JOIN request and add LEAVE update to batch.
 * N.B. response is sent before batch updates are applied.
 */
static void leave_request_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct groups *g = arg;
    const char *name;
    struct group *group;
    char errbuf[256];
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (!flux_msg_is_local (msg)) {
        errno = EPROTO;
        errmsg = "groups.leave is restricted to the local broker";
        goto error;
    }
    if (!(group = group_lookup (g, name, false))
        || !group->join_request) {
        snprintf (errbuf,
                  sizeof (errbuf),
                  "rank %lu is not a member of %s",
                  (unsigned long)g->ctx->rank,
                  name);
        errmsg = errbuf;
        errno = ENOENT;
        goto error;
    }
    if (groups_leave (g, name) < 0)
        goto error;
    flux_msg_decref (group->join_request);
    group->join_request = NULL;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to groups.join request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to groups.join request");
}

/* Respond to a request for group membership.
 * Return 0 on success, -1 on error with errno set.
 * A failure to respond is just logged, not treated as an error.
 */
static int get_respond_one (struct groups *g,
                            struct group *group,
                            const flux_msg_t *msg)
{
    char *s;

    if (!(s = idset_encode (group->members, IDSET_FLAG_RANGE)))
        return -1;
    if (flux_respond_pack (g->ctx->h, msg, "{s:s}", "members", s) < 0) {
        if (errno != ENOSYS) {
            flux_log_error (g->ctx->h,
                            "error responding to groups.get request");
        }
    }
    free (s);
    return 0;
}

/* 'group' membership has changed, respond to all pending groups.get
 * requests.
 */
static void get_respond_all (struct groups *g, struct group *group)
{
    const flux_msg_t *request;

    request = flux_msglist_first (group->watchers);
    while (request) {
        if (get_respond_one (g, group, request) < 0) {
            flux_log_error (g->ctx->h,
                            "error constructing groups.get response");
        }
        request = flux_msglist_next (group->watchers);
    }
}

/* Process a groups.get request for group membership.  To avoid bugs arising
 * from users making the request with nodeid=FLUX_NODEID_ANY on rank > 0,
 * which would return that broker's subtree membership, reject such requests.
 * If the group doesn't exist, it is created, empty.  This request may
 * optionally specify the FLUX_RPC_STREAMING flag, to "watch" a group.
 * Currently the streaming RPC is only terminated upon client disconnect.
 */
static void get_request_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct groups *g = arg;
    const char *name;
    struct group *group;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (g->ctx->rank != 0) {
        errmsg = "this RPC is only available on rank 0";
        errno = EPROTO;
        goto error;
    }
    if (!(group = group_lookup (g, name, true)))
        goto error;
    if (get_respond_one (g, group, msg) < 0)
        goto error;
    if (flux_msg_is_streaming (msg)) {
        if (flux_msglist_append (group->watchers, msg) < 0)
            goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0) {
        if (errno != ENOSYS)
            flux_log_error (h, "error responding to groups.get request");
    }
}

/* A client has disconnected.  Find any groups with a cached JOIN request
 * that matches the identity of the disconnecting client, and LEAVE those
 * groups.  Also, drop streaming get requests that match the disconnecting
 * client.
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct groups *g = arg;
    struct group *group;

    batch_flush (g); // handle any JOINs before disconnects

    group = zhashx_first (g->groups);
    while (group) {
        if (group->join_request
            && flux_disconnect_match (msg, group->join_request)) {
            if (groups_leave (g, group->name) < 0) {
                flux_log_error (h,
                                "groups: error disconnecting from %s",
                                group->name);
            }
            flux_msg_decref (group->join_request);
            group->join_request = NULL;
        }
        if (flux_msglist_disconnect (group->watchers, msg) < 0) {
            flux_log_error (h,
                            "groups: error disconnecting watchers of group=%s",
                            group->name);
        }
        group = zhashx_next (g->groups);
    }
}

/* Recursive function to walk 'topology', adding all subtree ranks to 'ids'.
 * Returns 0 on success, -1 on failure (errno is not set).
 */
static int add_subtree_ids (struct idset *ids, json_t *topology)
{
    int rank;
    json_t *a;
    size_t index;
    json_t *entry;

    if (json_unpack (topology, "{s:i s:o}", "rank", &rank, "children", &a) < 0
        || idset_set (ids, rank) < 0)
        return -1;
    json_array_foreach (a, index, entry) {
        if (add_subtree_ids (ids, entry) < 0)
            return -1;
    }
    return 0;
}

/* Generate JOIN/LEAVE for 'rank' in 'broker.torpid' group if rank becomes
 * torpid/non-torpid.  N.B. For now, just operate on the single rank, not
 * its entire subtree.  Although it would be straightforward to add the subtree
 * to the group when the root becomes torpid, removing the whole subtree when
 * responsiveness returns is less clear, since only a broker's immediate
 * parent really knows how responsive it is.
 */
static void torpid_update (struct groups *g,
                           uint32_t rank,
                           struct idset *subtree_ids,
                           bool torpid)
{
    struct idset *ids;
    bool set_flag;
    json_t *update = NULL;

    if (torpid && !idset_test (g->torpid, rank))
        set_flag = true;
    else if (!torpid && idset_test (g->torpid, rank))
        set_flag = false;
    else
        return; // nothing to do

    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_set (ids, rank) < 0
        || !(update = update_encode (ids, set_flag))
        || batch_append (g, "broker.torpid", update) < 0
        || (set_flag ? idset_set (g->torpid, rank)
                     : idset_clear (g->torpid, rank)) < 0) {
        flux_log_error (g->ctx->h, "error updating broker.torpid");
    }
    idset_destroy (ids);
    json_decref (update);
}

static void auto_leave (struct groups *g,
                        const char *status,
                        uint32_t rank,
                        struct idset *ids)
{
    struct group *group;

    group = zhashx_first (g->groups);
    while (group) {
        struct idset *x;
        json_t *update;

        if ((x = idset_intersect (group->members, ids))
            && idset_count (x) > 0) {
            if (!(update = update_encode (x, false))
                || batch_append (g, group->name, update) < 0) {
                flux_log_error (g->ctx->h,
                        "groups: error auto-updating %s on subtree loss",
                        group->name);
            }
            json_decref (update);
        }
        idset_destroy (x);
        group = zhashx_next (g->groups);
    }
}

static void overlay_monitor_cb (struct overlay *ov, uint32_t rank, void *arg)
{
    struct groups *g = arg;
    const char *status = overlay_get_subtree_status (ov, rank);
    json_t *topology;
    struct idset *ids = NULL;

    batch_flush (g); // handle any pending ops first

    /* Prepare a list of ranks that are members of subtree rooted at rank.
     */
    if (!(topology = overlay_get_subtree_topo (ov, rank))
        || !(ids = idset_create (0, IDSET_FLAG_AUTOGROW))
        || add_subtree_ids (ids, topology) < 0)
        goto done;

    /* Generate LEAVEs for any groups 'rank' (and subtree) may be a member
     * of if transitioning to lost (crashed) or offline (shutdown).
     */
    if (streq (status, "lost")
        || streq (status, "offline")) {
        auto_leave (g, status, rank, ids);
    }
    /* Update broker.torpid if torpidity has changed while subtree is in
     * one of the "online" states.
     */
    else if (streq (status, "full")
        || streq (status, "partial")
        || streq (status, "degraded")) {
        torpid_update (g, rank, ids, overlay_peer_is_torpid (ov, rank));
    }

done:
    idset_destroy (ids);
    json_decref (topology);
    return;
}

static const struct flux_msg_handler_spec htab[] = {
    {   FLUX_MSGTYPE_REQUEST,
        "groups.update",
        update_request_cb,
        0
    },
    {   FLUX_MSGTYPE_REQUEST,
        "groups.join",
        join_request_cb,
        0
    },
    {   FLUX_MSGTYPE_REQUEST,
        "groups.leave",
        leave_request_cb,
        0
    },
    {   FLUX_MSGTYPE_REQUEST,
        "groups.get",
        get_request_cb,
        FLUX_ROLE_USER,
    },
    {   FLUX_MSGTYPE_REQUEST,
        "groups.disconnect",
        disconnect_cb,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void groups_destroy (struct groups *g)
{
    if (g) {
        int saved_errno = errno;
        zhashx_destroy (&g->groups);
        json_decref (g->batch);
        idset_destroy (g->self);
        idset_destroy (g->torpid);
        flux_msg_handler_delvec (g->handlers);
        flux_watcher_destroy (g->batch_timer);
        free (g);
        errno = saved_errno;
    }
}

struct groups *groups_create (struct broker *ctx)
{
    struct groups *g;

    if (!(g = calloc (1, sizeof (*g))))
        return NULL;
    g->ctx = ctx;
    if (!(g->batch = json_object ())
        || !(g->groups = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (!(g->self = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_set (g->self, g->ctx->rank) < 0
        || !(g->torpid = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto error;
    zhashx_set_destructor (g->groups, group_destructor);
    zhashx_set_key_duplicator (g->groups, NULL);
    zhashx_set_key_destructor (g->groups, NULL);
    if (flux_msg_handler_addvec (ctx->h, htab, g, &g->handlers) < 0)
        goto error;
    if (!(g->batch_timer = flux_timer_watcher_create (flux_get_reactor (ctx->h),
                                                      0.,
                                                      0.,
                                                      batch_timeout_cb,
                                                      g)))
        goto error;
    overlay_set_monitor_cb (ctx->overlay, overlay_monitor_cb, g);
    return g;
error:
    groups_destroy (g);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
