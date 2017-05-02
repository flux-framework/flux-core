/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>
#include "namespace.h"

#include "src/common/libutil/iterators.h"

struct namespace {
    int flags;
    int seq;
    json_t *object;
    bool slave;
    char *name;
    uint32_t userid;
};

struct namespace_context {
    zhash_t *spaces;
    zlist_t *waiters;
};

static void namespace_destroy (void *arg)
{
    struct namespace *ns = arg;
    if (ns) {
        int saved_errno = errno;
        free (ns->name);
        json_decref (ns->object);
        free (ns);
        errno = saved_errno;
    }
}

static struct namespace *namespace_create (const char *name,
                                           uint32_t userid, int flags)
{
    struct namespace *ns;
    if (!(ns = calloc (1, sizeof (*ns))))
        goto nomem;
    if (!(ns->name = strdup (name)))
        goto nomem;
    ns->flags = flags;
    ns->userid = userid;
    ns->seq = -1;
    return ns;
nomem:
    errno = ENOMEM;
    namespace_destroy (ns);
    return NULL;
}

static int request_save (zlist_t *l, const flux_msg_t *msg)
{
    flux_msg_t *cpy;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto error;
    if (zlist_append (l, cpy) < 0)
        goto nomem;
    return 0;
nomem:
    errno = ENOMEM;
error:
    flux_msg_destroy (cpy);
    return -1;
}

static int request_restore_all (zlist_t *l, flux_t *h)
{
    flux_msg_t *msg;

    while ((msg = zlist_head (l))) {
        if (flux_requeue (h, msg, FLUX_RQ_TAIL) < 0)
            return -1;
        zlist_pop (l);
        flux_msg_destroy (msg);
    }
    return 0;
}

static int request_remove_from (zlist_t *l, const char *id)
{
    zlist_t *tmp;
    flux_msg_t *msg;
    char *msg_id;
    int rc = -1;

    if (!(tmp = zlist_dup (l))) { // N.B. items are not duplicated
        errno = ENOMEM;
        goto done;
    }
    FOREACH_ZLIST (tmp, msg) {
        if (flux_msg_get_route_first (msg, &msg_id) < 0)
            goto done;
        if (!strcmp (id, msg_id)) {
            zlist_remove (l, msg);
            flux_msg_destroy (msg);
        }
        free (msg_id);
    }
    rc = 0;
done:
    zlist_destroy (&tmp);
    return rc;
}

static void stats_get_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;

    if (flux_respondf (h, msg, "{s:i s:i}",
                               "waiters", zlist_size (ctx->waiters),
                               "namespaces", zhash_size (ctx->spaces)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void disconnect_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    char *id = NULL;

    if (flux_msg_get_route_first (msg, &id) < 0) {
        flux_log_error (h, "%s: could not determine sender", __FUNCTION__);
        goto done;
    }
    if (request_remove_from (ctx->waiters, id) < 0) {
        flux_log_error (h, "%s: flux_remove_from %s", __FUNCTION__, id);
        goto done;
    }
done:
    free (id);
}

static void allcommit_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    struct namespace *ns;
    const char *name, *topic;
    int seq;
    json_t *object;
    const int prefix_length = strlen ("ns.allcommit.");
    uint32_t userid, rolemask;

    if (flux_event_decodef (msg, &topic, "{s:i s:o}",
                            "seq", &seq,
                            "object", &object) < 0) {
        flux_log_error (h, "%s: error decoding event", __FUNCTION__);
        return;
    }
    if (strlen (topic) <= prefix_length) {
        flux_log(h, LOG_ERR, "%s: %s topic too short", __FUNCTION__, topic);
        return;
    }
    name = topic + prefix_length;
    if (!(ns = zhash_lookup (ctx->spaces, name)) || !ns->slave)
        return;

    /* security check */
    if (flux_msg_get_userid (msg, &userid) < 0)
        return;
    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        return;
    if (!(rolemask & FLUX_ROLE_OWNER) && (ns->userid == FLUX_USERID_UNKNOWN
                                       || ns->userid != userid)) {
        flux_log (h, LOG_ERR, "%s: commit %s: permission denied",
                  __FUNCTION__, name);
        return;
    }

    /* sequence must not regress */
    if (seq <= ns->seq) {
        flux_log (h, LOG_ERR, "%s: commit %s: invalid sequence (%d->%d)",
                  __FUNCTION__, name, ns->seq, seq);
        return;
    }
    json_decref (ns->object);
    ns->object = json_incref (object);
    ns->seq = seq;
    if (request_restore_all (ctx->waiters, h) < 0) {
        flux_log_error (h, "%s: create %s: requeuing waiters",
                        __FUNCTION__, name);
    }
}

static void commit_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    const char *name;
    int seq;
    json_t *object;
    struct namespace *ns;
    flux_msg_t *event = NULL;
    char *topic = NULL;
    uint32_t userid, rolemask;

    if (flux_request_decodef (msg, NULL, "{s:s s:i s:o}",
                              "name", &name,
                              "seq", &seq,
                              "object", &object) < 0)
        goto error;
    if (!(ns = zhash_lookup (ctx->spaces, name))) {
        errno = ENOENT;
        goto error;
    }
    if (ns->slave || seq != ns->seq + 1) {
        errno = EINVAL;
        goto error;
    }

    /* security check */
    if (flux_msg_get_userid (msg, &userid) < 0)
        goto error;
    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (!(rolemask & FLUX_ROLE_OWNER) && (ns->userid == FLUX_USERID_UNKNOWN
                                       || ns->userid != userid)) {
        errno = EPERM;
        goto error;
    }

    json_decref (ns->object);
    ns->object = json_incref (object);
    ns->seq = seq;
    if ((ns->flags & FLUX_NS_SYNCHRONIZE)) {
        if (asprintf (&topic, "ns.allcommit.%s", name) < 0)
            goto nomem;
        if (!(event = flux_event_encodef (topic, "{s:i s:i s:O}",
                                        "flags", ns->flags,
                                        "seq", ns->seq,
                                        "object", ns->object)))
            goto error;
        if (ns->userid != FLUX_USERID_UNKNOWN) {
            if (flux_msg_set_userid (event, ns->userid) < 0)
                goto error;
        }
        if (flux_msg_set_private (event) < 0) // private to namespace owner
            goto error;                       //  (and instance owner)
        if (flux_send (h, event, 0) < 0)
            goto error;
    }
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_msg_destroy (event);
    if (request_restore_all (ctx->waiters, h) < 0) {
        flux_log_error (h, "%s: create %s: requeuing waiters",
                        __FUNCTION__, name);
    }
    free (topic);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_msg_destroy (event);
    free (topic);
}

static void lookup_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    const char *name;
    struct namespace *ns;
    int flags;
    int min_seq;
    uint32_t userid, rolemask;

    if (flux_request_decodef (msg, NULL, "{s:s s:i s:i}",
                              "name", &name,
                              "min_seq", &min_seq,
                              "flags", &flags) < 0)
        goto error;
    if (min_seq < 0) {
        errno = EINVAL;
        goto error;
    }
    if (!(ns = zhash_lookup (ctx->spaces, name)) || ns->seq < min_seq) {
        if ((flags & FLUX_NS_WAIT)) {
            if (request_save (ctx->waiters, msg) < 0)
                goto error;
            return;
        }
        else {
            errno = ENOENT;
            goto error;
        }
    }

    /* security check */
    if (flux_msg_get_userid (msg, &userid) < 0)
        goto error;
    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (!(rolemask & FLUX_ROLE_OWNER) && (ns->userid == FLUX_USERID_UNKNOWN
                                       || ns->userid != userid)) {
        errno = EPERM;
        goto error;
    }

    if (flux_respondf (h, msg, "{s:i s:O}", "seq", ns->seq,
                                            "object", ns->object) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void allcreate_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    struct namespace *ns;
    const char *name;
    uint32_t userid;
    int flags;

    if (flux_event_decodef (msg, NULL, "{s:s s:i s:i}",
                            "name", &name,
                            "userid", &userid,
                            "flags", &flags) < 0) {
        flux_log_error (h, "%s: error decoding event", __FUNCTION__);
        return;
    }
    if ((ns = zhash_lookup (ctx->spaces, name)))
        return;
    if (!(ns = namespace_create (name, userid, flags))) {
        flux_log_error (h, "%s: create %s", __FUNCTION__, name);
        return;
    }
    ns->slave = true;
    if (zhash_insert (ctx->spaces, name, ns) < 0) {
        flux_log (h, LOG_ERR, "%s: create %s: out of memory",
                  __FUNCTION__, name);
        namespace_destroy (ns);
        return;
    }
    zhash_freefn (ctx->spaces, name, namespace_destroy);
    if (request_restore_all (ctx->waiters, h) < 0) {
        flux_log_error (h, "%s: create %s: requeuing waiters",
                        __FUNCTION__, name);
    }
}

static void create_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    struct namespace *ns = NULL;
    int flags;
    char *name;
    flux_msg_t *event = NULL;
    uint32_t userid;

    if (flux_request_decodef (msg, NULL, "{s:s s:i s:i}",
                              "name", &name,
                              "userid", &userid,
                              "flags", &flags) < 0)
        goto error;
    if (zhash_lookup (ctx->spaces, name)) {
        errno = EEXIST;
        goto error;
    }
    if (!(ns = namespace_create (name, userid, flags)))
        goto error;
    if (zhash_insert (ctx->spaces, ns->name, ns) < 0) {
        namespace_destroy (ns);
        goto nomem;
    }
    zhash_freefn (ctx->spaces, ns->name, namespace_destroy);
    if ((ns->flags & FLUX_NS_SYNCHRONIZE)) {
        if (!(event = flux_event_encodef ("ns.allcreate", "{s:s s:i s:i}",
                                          "name", name,
                                          "userid", userid,
                                          "flags", flags)))
            goto error;
        if (flux_msg_set_private (event) < 0)
            goto error;
        if (flux_send (h, event, 0) < 0)
            goto error;
    }
    flux_msg_destroy (event);
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_msg_destroy (event);
}

static void allremove_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    struct namespace *ns;
    const char *name;

    if (flux_event_decodef (msg, NULL, "{s:s}",
                            "name", &name) < 0) {
        flux_log_error (h, "%s: error decoding event", __FUNCTION__);
        return;
    }
    if ((ns = zhash_lookup (ctx->spaces, name)) && ns->slave)
        zhash_delete (ctx->spaces, name);
}

static void remove_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    struct namespace_context *ctx = arg;
    struct namespace *ns;
    const char *name;
    flux_msg_t *event = NULL;

    if (flux_request_decodef (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (!(ns = zhash_lookup (ctx->spaces, name))) {
        errno = ENOENT;
        goto error;
    }
    if ((ns->flags & FLUX_NS_SYNCHRONIZE)) {
        if (!(event = flux_event_encodef ("ns.allremove",
                                          "{s:s}", "name", name)))
            goto error;
        if (flux_msg_set_private (event) < 0)
            goto error;
        if (flux_send (h, event, 0) < 0)
            goto error;
    }
    zhash_delete (ctx->spaces, name);
    flux_msg_destroy (event);
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_msg_destroy (event);
}

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "ns.create",    create_cb,    0, NULL },
    { FLUX_MSGTYPE_EVENT,   "ns.allcreate", allcreate_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "ns.remove",    remove_cb,    0, NULL },
    { FLUX_MSGTYPE_EVENT,   "ns.allremove", allremove_cb, 0, NULL },

    { FLUX_MSGTYPE_REQUEST, "ns.commit",     commit_cb,  FLUX_ROLE_ALL, NULL },
    { FLUX_MSGTYPE_EVENT, "ns.allcommit.*",  allcommit_cb,
                                                         FLUX_ROLE_ALL, NULL },
    { FLUX_MSGTYPE_REQUEST, "ns.lookup",     lookup_cb,  FLUX_ROLE_ALL, NULL },
    { FLUX_MSGTYPE_REQUEST, "ns.disconnect", disconnect_cb,
                                                         FLUX_ROLE_ALL, NULL },
    { FLUX_MSGTYPE_REQUEST, "ns.stats.get", stats_get_cb,
                                                         FLUX_ROLE_ALL, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

static void namespace_finalize (void *arg)
{
    struct namespace_context *ctx = arg;
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (handlers);
        zhash_destroy (&ctx->spaces);
        if (ctx->waiters) {
            flux_msg_t *msg;
            while ((msg = zlist_pop (ctx->waiters)))
                flux_msg_destroy (msg);
            zlist_destroy (&ctx->waiters);
        }
        free (ctx);
        errno = saved_errno;
    }
}

int namespace_initialize (flux_t *h)
{
    struct namespace_context *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        goto nomem;
    if (!(ctx->spaces = zhash_new ()))
        goto nomem;
    if (!(ctx->waiters = zlist_new ()))
        goto nomem;
    if (flux_msg_handler_addvec (h, handlers, ctx) < 0)
        goto error;
    if (flux_event_subscribe (h, "ns.") < 0)
        goto error;
    flux_aux_set (h, "flux::namespace", ctx, namespace_finalize);
    return 0;
nomem:
    errno = ENOMEM;
error:
    namespace_finalize (ctx);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
