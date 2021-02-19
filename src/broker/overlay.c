/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdarg.h>
#include <czmq.h>
#include <zmq.h>
#include <flux/core.h>
#include <inttypes.h>
#include <jansson.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/fsd.h"

#include "overlay.h"
#include "attr.h"

#define FLUX_ZAP_DOMAIN "flux"
#define ZAP_ENDPOINT "inproc://zeromq.zap.01"

struct endpoint {
    zsock_t *zsock;
    char *uri;
    flux_watcher_t *w;
};

struct child {
    int lastseen;
    unsigned long rank;
    char uuid[16];
    bool connected;
    bool idle;
};

/* Wake up periodically (between 'sync_min' and 'sync_max' seconds) and:
 * 1) send keepalive to parent if nothing was sent in 'idle_min' seconds
 * 2) find children that have not been heard from in 'idle_max' seconds
 */
static const double sync_min = 1.0;
static const double sync_max = 5.0;

static const double idle_min = 5.0;
static const double idle_max = 30.0;

struct overlay {
    zcert_t *cert;
    zcertstore_t *certstore;
    zsock_t *zap;
    flux_watcher_t *zap_w;

    flux_t *h;
    flux_msg_handler_t **handlers;
    flux_future_t *f_sync;

    uint32_t size;
    uint32_t rank;
    int tbon_k;
    char uuid[16];

    struct child *children;
    int child_count;

    struct endpoint *parent;    /* DEALER - requests to parent */
    overlay_sock_cb_f parent_cb;
    void *parent_arg;
    int parent_lastsent;
    char *parent_pubkey;

    struct endpoint *child;     /* ROUTER - requests from children */
    overlay_sock_cb_f child_cb;
    void *child_arg;

    overlay_monitor_cb_f child_monitor_cb;
    void *child_monitor_arg;

    overlay_init_cb_f init_cb;
    void *init_arg;
};

/* Convenience iterator for ov->children
 */
#define foreach_overlay_child(ov, child) \
    for ((child) = &(ov)->children[0]; \
            (child) - &(ov)->children[0] < (ov)->child_count; \
            (child)++)

static void overlay_monitor_notify (struct overlay *ov)
{
    if (ov->child_monitor_cb)
        ov->child_monitor_cb (ov, ov->child_monitor_arg);
}

static void endpoint_destroy (struct endpoint *ep)
{
    if (ep) {
        int saved_errno = errno;
        free (ep->uri);
        flux_watcher_destroy (ep->w);
        zsock_destroy (&ep->zsock);
        free (ep);
        errno = saved_errno;
    }
}

static struct endpoint *endpoint_create (const char *uri)
{
    struct endpoint *ep;

    if (!(ep = calloc (1, sizeof (*ep))))
        return NULL;
    if (!(ep->uri = strdup (uri)))
        goto error;
    return ep;
error:
    endpoint_destroy (ep);
    return NULL;
}

void overlay_set_init_callback (struct overlay *ov,
                                overlay_init_cb_f cb,
                                void *arg)
{
    ov->init_cb = cb;
    ov->init_arg = arg;
}

/* Allocate children array based on static tree topology, and return size.
 */
static int alloc_children (uint32_t rank,
                           uint32_t size,
                           int k,
                           struct child **chp)
{
    struct child *ch = NULL;
    int count;
    int i;

    for (count = 0; kary_childof (k, size, rank, count) != KARY_NONE; count++)
        ;
    if (count > 0) {
        if (!(ch = calloc (count, sizeof (*ch))))
            return -1;
        for (i = 0; i < count; i++) {
            ch[i].rank = kary_childof (k, size, rank, i);
            snprintf (ch[i].uuid, sizeof (ch[i].uuid), "%lu", ch[i].rank);
        }
    }
    *chp = ch;
    return count;
}

int overlay_init (struct overlay *ov,
                  uint32_t size,
                  uint32_t rank,
                  int tbon_k)
{
    ov->size = size;
    ov->rank = rank;
    ov->tbon_k = tbon_k;
    if ((ov->child_count = alloc_children (rank,
                                           size,
                                           tbon_k,
                                           &ov->children)) < 0)
        return -1;
    snprintf (ov->uuid, sizeof (ov->uuid), "%"PRIu32, rank);
    if (ov->init_cb)
        return (*ov->init_cb) (ov, ov->init_arg);

    return 0;
}

uint32_t overlay_get_rank (struct overlay *ov)
{
    return ov->rank;
}

uint32_t overlay_get_size (struct overlay *ov)
{
    return ov->size;
}

int overlay_get_child_peer_count (struct overlay *ov)
{
    struct child *child;
    int count = 0;

    foreach_overlay_child (ov, child) {
        if (child->connected)
            count++;
    }
    return count;
}

void overlay_log_idle_children (struct overlay *ov)
{
    struct child *child;
    double now = flux_reactor_now (flux_get_reactor (ov->h));
    char fsd[64];
    int idle;

    if (idle_max > 0) {
        foreach_overlay_child (ov, child) {
            if (child->connected) {
                idle = now - child->lastseen;

                if (idle >= idle_max) {
                    (void)fsd_format_duration (fsd, sizeof (fsd), idle);
                    if (!child->idle) {
                        flux_log (ov->h,
                                  LOG_ERR,
                                  "child %lu idle for %s",
                                  child->rank,
                                  fsd);
                        child->idle = true;
                    }
                }
                else {
                    if (child->idle) {
                        flux_log (ov->h,
                                  LOG_ERR,
                                  "child %lu no longer idle",
                                  child->rank);
                        child->idle = false;
                    }
                }
            }
        }
    }
}

static struct child *child_lookup (struct overlay *ov, const char *uuid)
{
    struct child *child;

    foreach_overlay_child (ov, child) {
        if (!strcmp (uuid, child->uuid))
            return child;
    }
    return NULL;
}

void overlay_keepalive_child (struct overlay *ov, const char *uuid, int status)
{
    struct child *child = child_lookup (ov, uuid);

    if (child) {
        bool prev_value = child->connected;

        if (status == KEEPALIVE_STATUS_NORMAL)
            child->connected = true;
        else // status == KEEPALIVE_STATUS_DISCONNECT
            child->connected = false;
        child->lastseen = flux_reactor_now (flux_get_reactor (ov->h));

        if (child->connected != prev_value)
            overlay_monitor_notify (ov);
    }
}

int overlay_set_parent_pubkey (struct overlay *ov, const char *pubkey)
{
    if (!(ov->parent_pubkey = strdup (pubkey)))
        return -1;
    return 0;
}

int overlay_set_parent_uri (struct overlay *ov, const char *uri)
{
    if (!(ov->parent = endpoint_create (uri)))
        return -1;
    return 0;
}

const char *overlay_get_parent_uri (struct overlay *ov)
{
    if (!ov->parent)
        return NULL;
    return ov->parent->uri;
}

int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->parent || !ov->parent->zsock) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = flux_msg_sendzsock (ov->parent->zsock, msg);
    if (rc == 0)
        ov->parent_lastsent = flux_reactor_now (flux_get_reactor (ov->h));
done:
    return rc;
}

flux_msg_t *overlay_recvmsg_parent (struct overlay *ov)
{
    return flux_msg_recvzsock (ov->parent->zsock);
}

static int overlay_keepalive_parent (struct overlay *ov, int status)
{
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (!ov->parent || !ov->parent->zsock)
        return 0;
    if (!(msg = flux_keepalive_encode (0, status)))
        goto done;
    if (flux_msg_enable_route (msg) < 0)
        goto done;
    rc = flux_msg_sendzsock (ov->parent->zsock, msg);
done:
    flux_msg_destroy (msg);
    return rc;
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct overlay *ov = arg;
    double now = flux_reactor_now (flux_get_reactor (ov->h));

    if (now - ov->parent_lastsent > idle_min)
        overlay_keepalive_parent (ov, KEEPALIVE_STATUS_NORMAL);
    overlay_log_idle_children (ov);

    flux_future_reset (f);
}

void overlay_set_parent_cb (struct overlay *ov, overlay_sock_cb_f cb, void *arg)
{
    ov->parent_cb = cb;
    ov->parent_arg = arg;
}


const char *overlay_get_bind_uri (struct overlay *ov)
{
    if (!ov->child)
        return NULL;
    return ov->child->uri;
}

void overlay_set_child_cb (struct overlay *ov, overlay_sock_cb_f cb, void *arg)
{
    ov->child_cb = cb;
    ov->child_arg = arg;
}

int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->child || !ov->child->zsock) {
        errno = EINVAL;
        goto done;
    }
    rc = flux_msg_sendzsock_ex (ov->child->zsock, msg, true);
done:
    return rc;
}

flux_msg_t *overlay_recvmsg_child (struct overlay *ov)
{
    return flux_msg_recvzsock (ov->child->zsock);
}

static int overlay_mcast_child_one (void *zsock,
                                    const flux_msg_t *msg,
                                    struct child *child)
{
    flux_msg_t *cpy;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_enable_route (cpy) < 0)
        goto done;
    if (flux_msg_push_route (cpy, child->uuid) < 0)
        goto done;
    if (flux_msg_sendzsock_ex (zsock, cpy, true) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (cpy);
    return rc;
}

void overlay_mcast_child (struct overlay *ov, const flux_msg_t *msg)
{
    struct child *child;
    int disconnects = 0;

    if (!ov->child || !ov->child->zsock)
        return;
    foreach_overlay_child (ov, child) {
        if (!child->connected)
            continue;
        if (overlay_mcast_child_one (ov->child->zsock, msg, child) < 0) {
            if (errno == EHOSTUNREACH) {
                child->connected = false;
                disconnects++;
            }
            else
                flux_log_error (ov->h,
                                "mcast error to child rank %lu",
                                child->rank);
        }
    }
    if (disconnects)
        overlay_monitor_notify (ov);
}

static void child_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    struct overlay *ov = arg;
    if (ov->child_cb)
        ov->child_cb (ov, ov->child_arg);
}

static void parent_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct overlay *ov = arg;

    if (ov->parent_cb)
        ov->parent_cb (ov, ov->parent_arg);
}

static zframe_t *get_zmsg_nth (zmsg_t *msg, int n)
{
    zframe_t *zf;
    int count = 0;

    zf = zmsg_first (msg);
    while (zf) {
        if (count++ == n)
            return zf;
        zf = zmsg_next (msg);
    }
    return NULL;
}

static bool streq_zmsg_nth (zmsg_t *msg, int n, const char *s)
{
    zframe_t *zf = get_zmsg_nth (msg, n);
    if (zf && zframe_streq (zf, s))
        return true;
    return false;
}

static bool pubkey_zmsg_nth (zmsg_t *msg, int n, char *pubkey_txt)
{
    zframe_t *zf = get_zmsg_nth (msg, n);
    if (!zf || zframe_size (zf) != 32)
        return false;
    zmq_z85_encode (pubkey_txt, zframe_data (zf), 32);
    return true;
}

static bool add_zmsg_nth (zmsg_t *dst, zmsg_t *src, int n)
{
    zframe_t *zf = get_zmsg_nth (src, n);
    if (!zf || zmsg_addmem (dst, zframe_data (zf), zframe_size (zf)) < 0)
        return false;
    return true;
}

/* ZAP 1.0 messages have the following parts
 * REQUEST                              RESPONSE
 *   0: version                           0: version
 *   1: sequence                          1: sequence
 *   2: domain                            2: status_code
 *   3: address                           3: status_text
 *   4: identity                          4: user_id
 *   5: mechanism                         5: metadata
 *   6: client_key
 */
static void overlay_zap_cb (flux_reactor_t *r,
                            flux_watcher_t *w,
                            int revents,
                            void *arg)
{
    struct overlay *ov = arg;
    zmsg_t *req = NULL;
    zmsg_t *rep = NULL;
    char pubkey[41];
    const char *status_code = "400";
    const char *status_text = "No access";
    const char *user_id = "";
    zcert_t *cert;
    const char *name = NULL;
    int log_level = LOG_ERR;

    if ((req = zmsg_recv (ov->zap))) {
        if (!streq_zmsg_nth (req, 0, "1.0")
                || !streq_zmsg_nth (req, 5, "CURVE")
                || !pubkey_zmsg_nth (req, 6, pubkey)) {
            log_err ("ZAP request decode error");
            goto done;
        }
        if ((cert = zcertstore_lookup (ov->certstore, pubkey)) != NULL) {
            status_code = "200";
            status_text = "OK";
            user_id = pubkey;
            name = zcert_meta (cert, "name");
            log_level = LOG_INFO;
        }
        if (!name)
            name = "unknown";
        flux_log (ov->h, log_level, "overlay auth %s %s", name, status_text);

        if (!(rep = zmsg_new ()))
            goto done;
        if (!add_zmsg_nth (rep, req, 0)
                || !add_zmsg_nth (rep, req, 1)
                || zmsg_addstr (rep, status_code) < 0
                || zmsg_addstr (rep, status_text) < 0
                || zmsg_addstr (rep, user_id) < 0
                || zmsg_addmem (rep, NULL, 0) < 0) {
            log_err ("ZAP response encode error");
            goto done;
        }
        if (zmsg_send (&rep, ov->zap) < 0)
            log_err ("ZAP send error");
    }
done:
    zmsg_destroy (&req);
    zmsg_destroy (&rep);
}

static int overlay_zap_init (struct overlay *ov)
{
    if (!(ov->zap = zsock_new (ZMQ_REP)))
        return -1;
    if (zsock_bind (ov->zap, ZAP_ENDPOINT) < 0) {
        errno = EINVAL;
        log_err ("could not bind to %s", ZAP_ENDPOINT);
        return -1;
    }
    if (!(ov->zap_w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                               ov->zap,
                                               FLUX_POLLIN,
                                               overlay_zap_cb,
                                               ov)))
        return -1;
    flux_watcher_start (ov->zap_w);
    return 0;
}

int overlay_connect (struct overlay *ov)
{
    if (!ov->h || ov->rank == FLUX_NODEID_ANY) {
        errno = EINVAL;
        return -1;
    }
    if (ov->parent) {
        if (!(ov->parent->zsock = zsock_new_dealer (NULL)))
            goto nomem;
        zsock_set_zap_domain (ov->parent->zsock, FLUX_ZAP_DOMAIN);
        zcert_apply (ov->cert, ov->parent->zsock);
        zsock_set_curve_serverkey (ov->parent->zsock, ov->parent_pubkey);
        zsock_set_identity (ov->parent->zsock, ov->uuid);
        if (zsock_connect (ov->parent->zsock, "%s", ov->parent->uri) < 0)
            goto nomem;
        if (!(ov->parent->w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                                       ov->parent->zsock,
                                                       FLUX_POLLIN,
                                                       parent_cb,
                                                       ov)))
        return -1;
        flux_watcher_start (ov->parent->w);
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

int overlay_bind (struct overlay *ov, const char *uri)
{
    if (!ov->h || ov->rank == FLUX_NODEID_ANY || ov->child) {
        errno = EINVAL;
        return -1;
    }
    if (!ov->zap && overlay_zap_init (ov) < 0)
        return -1;
    if (!(ov->child = endpoint_create (uri)))
        return -1;
    if (!(ov->child->zsock = zsock_new_router (NULL)))
        return -1;
    zsock_set_router_mandatory (ov->child->zsock, 1);

    zsock_set_zap_domain (ov->child->zsock, FLUX_ZAP_DOMAIN);
    zcert_apply (ov->cert, ov->child->zsock);
    zsock_set_curve_server (ov->child->zsock, 1);

    if (zsock_bind (ov->child->zsock, "%s", ov->child->uri) < 0)
        return -1;
    if (strchr (ov->child->uri, '*')) { /* capture dynamically assigned port */
        char *newuri = zsock_last_endpoint (ov->child->zsock);
        if (!newuri)
            return -1;
        free (ov->child->uri);
        ov->child->uri = newuri;
    }
    if (!(ov->child->w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                                  ov->child->zsock,
                                                  FLUX_POLLIN,
                                                  child_cb,
                                                  ov)))
        return -1;
    flux_watcher_start (ov->child->w);
    /* Ensure that ipc files are removed when the broker exits.
     */
    char *ipc_path = strstr (ov->child->uri, "ipc://");
    if (ipc_path)
        cleanup_push_string (cleanup_file, ipc_path + 6);
    return 0;
}

/* A callback of type attr_get_f to allow retrieving some information
 * from an struct overlay through attr_get().
 */
static int overlay_attr_get_cb (const char *name, const char **val, void *arg)
{
    struct overlay *overlay = arg;
    int rc = -1;

    if (!strcmp (name, "tbon.parent-endpoint"))
        *val = overlay_get_parent_uri (overlay);
    else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int overlay_register_attrs (struct overlay *overlay, attr_t *attrs)
{
    int tbon_level = kary_levelof (overlay->tbon_k, overlay->rank);
    int tbon_maxlevel = kary_levelof (overlay->tbon_k, overlay->size - 1);
    int tbon_descendants = kary_sum_descendants (overlay->tbon_k,
                                                 overlay->size,
                                                 overlay->rank);

    if (attr_add_active (attrs, "tbon.parent-endpoint",
                         FLUX_ATTRFLAG_READONLY,
                         overlay_attr_get_cb, NULL, overlay) < 0)
        return -1;
    if (attr_add_uint32 (attrs, "rank", overlay->rank,
                         FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_uint32 (attrs, "size", overlay->size,
                         FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.arity", overlay->tbon_k,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.level", tbon_level,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.maxlevel", tbon_maxlevel,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.descendants", tbon_descendants,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;

    return 0;
}

void overlay_set_monitor_cb (struct overlay *ov,
                             overlay_monitor_cb_f cb,
                             void *arg)
{
    ov->child_monitor_cb = cb;
    ov->child_monitor_arg = arg;
}

static json_t *lspeer_object_create (struct overlay *ov)
{
    json_t *o = NULL;
    json_t *child_o;
    double now = flux_reactor_now (flux_get_reactor (ov->h));
    struct child *child;

    if (!(o = json_object ()))
        goto nomem;
    foreach_overlay_child (ov, child) {
        if (!(child_o = json_pack ("{s:f}",
                                   "idle",
                                   now - child->lastseen)))
            goto nomem;
        if (json_object_set_new (o, child->uuid, child_o) < 0) {
            json_decref (child_o);
            goto nomem;
        }
    }
    return o;
nomem:
    json_decref (o);
    errno = ENOMEM;
    return NULL;
}

static void lspeer_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct overlay *ov = arg;
    json_t *o;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(o = lspeer_object_create (ov)))
        goto error;
    if (flux_respond_pack (h, msg, "O", o) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    json_decref (o);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

int overlay_cert_load (struct overlay *ov, const char *path)
{
    struct stat sb;
    zcert_t *cert;

    if (stat (path, &sb) < 0) {
        log_err ("%s", path);
        return -1;
    }
    if ((sb.st_mode & S_IROTH) | (sb.st_mode & S_IRGRP)) {
        log_msg ("%s: readable by group/other", path);
        errno = EPERM;
        return -1;
    }
    if (!(cert = zcert_load (path))) {
        log_msg ("%s: invalid CURVE certificate", path);
        errno = EINVAL;
        return -1;
    }
    zcert_destroy (&ov->cert);
    ov->cert = cert;
    return 0;
}

const char *overlay_cert_pubkey (struct overlay *ov)
{
    return zcert_public_txt (ov->cert);
}

const char *overlay_cert_name (struct overlay *ov)
{
    return zcert_meta (ov->cert, "name");
}

/* Create a zcert_t and add it to in-memory zcertstore_t.
 */
int overlay_authorize (struct overlay *ov, const char *name, const char *pubkey)
{
    uint8_t public_key[32];
    zcert_t *cert;

    if (strlen (pubkey) != 40 || !zmq_z85_decode (public_key, pubkey)) {
        errno = EINVAL;
        return -1;
    }
    if (!(cert = zcert_new_from (public_key, public_key))) {
        errno = ENOMEM;
        return -1;
    }
    zcert_set_meta (cert, "name", "%s", name);
    zcertstore_insert (ov->certstore, &cert); // takes ownership of cert
    return 0;
}

void overlay_destroy (struct overlay *ov)
{
    if (ov) {
        int saved_errno = errno;

        zcert_destroy (&ov->cert);
        flux_watcher_destroy (ov->zap_w);
        if (ov->zap) {
            zsock_unbind (ov->zap, ZAP_ENDPOINT);
            zsock_destroy (&ov->zap);
        }
        zcertstore_destroy (&ov->certstore);

        flux_future_destroy (ov->f_sync);
        flux_msg_handler_delvec (ov->handlers);
        overlay_keepalive_parent (ov, KEEPALIVE_STATUS_DISCONNECT);
        endpoint_destroy (ov->parent);
        endpoint_destroy (ov->child);
        free (ov->parent_pubkey);
        free (ov->children);
        free (ov);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "overlay.lspeer", lspeer_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct overlay *overlay_create (flux_t *h)
{
    struct overlay *ov;

    if (!(ov = calloc (1, sizeof (*ov))))
        return NULL;
    ov->rank = FLUX_NODEID_ANY;
    ov->parent_lastsent = -1;
    ov->h = h;

    if (flux_msg_handler_addvec (h, htab, ov, &ov->handlers) < 0)
        goto error;
    if (!(ov->f_sync = flux_sync_create (h, sync_min))
        || flux_future_then (ov->f_sync, sync_max, sync_cb, ov) < 0)
        goto error;
    if (!(ov->cert = zcert_new ()))
        goto nomem;
    if (!(ov->certstore = zcertstore_new (NULL)))
        goto nomem;
    return ov;
nomem:
    errno = ENOMEM;
error:
    overlay_destroy (ov);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
