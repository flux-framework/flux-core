/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* zap.c - zeromq auth proto (ZAP) server, embeddable in a flux reactor loop
 *
 * See 0MQ RFC 27:  https://rfc.zeromq.org/spec/27/
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <zmq.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "zwatcher.h"
#include "sockopt.h"
#include "mpart.h"
#include "cert.h"
#include "zap.h"

#define ZAP_ENDPOINT "inproc://zeromq.zap.01"

struct zmqutil_zap {
    zhashx_t *certstore; // public_txt => authorized cert
    void *sock;
    flux_watcher_t *w;
    zaplog_f logger;
    void *logger_arg;
};

/* Get a public CURVE key (binary form) from message part at index 'n',
 * and convert it to 40 byte text.  Return 0 on success, -1 on failure.
 */
static int get_mpart_pubkey (zlist_t *mpart, int n, char *pubkey_txt)
{
    zmq_msg_t *part = mpart_get (mpart, n);
    if (!part || zmq_msg_size (part) != 32)
        return -1;
    zmq_z85_encode (pubkey_txt, zmq_msg_data (part), zmq_msg_size (part));
    return 0;
}

/* Take a copy of message part at index 'n' of 'src' and append it to 'dst'.
 * Return 0 on success, -1 on failure.
 */
static int add_mpart_copy (zlist_t *dst, zlist_t *src, int n)
{
    zmq_msg_t *part = mpart_get (src, n);
    if (!part)
        return -1;
    return mpart_addmem (dst, zmq_msg_data (part), zmq_msg_size (part));
}

static void logger (struct zmqutil_zap *zap,
                    int severity,
                    const char *fmt, ...)
{
    if (zap->logger) {
        va_list ap;
        char buf[256];

        va_start (ap, fmt);
        vsnprintf (buf, sizeof (buf), fmt, ap);
        va_end (ap);

        zap->logger (severity, buf, zap->logger_arg);
    }
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
static void zap_cb (flux_reactor_t *r,
                    flux_watcher_t *w,
                    int revents,
                    void *arg)
{
    struct zmqutil_zap *zap = arg;
    zlist_t *req = NULL;
    zlist_t *rep = NULL;
    char pubkey[41];
    const char *status_code = "400";
    const char *status_text = "No access";
    const char *user_id = "";
    struct cert *cert;

    if ((req = mpart_recv (zap->sock))) {
        if (!mpart_streq (req, 0, "1.0")
            || !mpart_streq (req, 5, "CURVE")
            || get_mpart_pubkey (req, 6, pubkey) < 0) {
            logger (zap, LOG_ERR, "ZAP request decode error");
            goto done;
        }
        if ((cert = zhashx_lookup (zap->certstore, pubkey)) != NULL) {
            status_code = "200";
            status_text = "OK";
            user_id = pubkey;
        }
        else
            logger (zap, LOG_ERR, "overlay auth %s", status_text);

        if (!(rep = mpart_create ()))
            goto done;
        if (add_mpart_copy (rep, req, 0) < 0
            || add_mpart_copy (rep, req, 1) < 0
            || mpart_addstr (rep, status_code) < 0
            || mpart_addstr (rep, status_text) < 0
            || mpart_addstr (rep, user_id) < 0
            || mpart_addmem (rep, NULL, 0) < 0) {
            logger (zap, LOG_ERR, "ZAP response encode error");
            goto done;
        }
        if (mpart_send (zap->sock, rep) < 0)
            logger (zap, LOG_ERR, "ZAP send error");
    }
done:
    mpart_destroy (req);
    mpart_destroy (rep);
}

/* Create a cert and add it to in-memory store.
 */
int zmqutil_zap_authorize (struct zmqutil_zap *zap,
                           const char *name,
                           const char *pubkey)
{
    struct cert *cert;

    if (!zap || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(cert = cert_create_from (pubkey, NULL))
        || cert_meta_set (cert, "name", name) < 0)
        return -1;

    // hash takes ownership of cert
    if ((zhashx_insert (zap->certstore, cert_public_txt (cert), cert)) < 0) {
        cert_destroy (cert);
        errno = EEXIST;
        return -1;
    }
    return 0;
}

void zmqutil_zap_set_logger (struct zmqutil_zap *zap, zaplog_f fun, void *arg)
{
    if (zap) {
        zap->logger = fun;
        zap->logger_arg = arg;
    }
}

void zmqutil_zap_destroy (struct zmqutil_zap *zap)
{
    if (zap) {
        int saved_errno = errno;
        flux_watcher_destroy (zap->w);
        if (zap->sock) {
            zmq_unbind (zap->sock, ZAP_ENDPOINT);
            zmq_close (zap->sock);
        }
        zhashx_destroy (&zap->certstore);
        free (zap);
        errno = saved_errno;
    }
}

// zhashx_destructor_fn footprint
void cert_destructor (void **item)
{
    if (item) {
        struct cert *cert = *item;
        cert_destroy (cert);
        *item = NULL;
    }
}

struct zmqutil_zap *zmqutil_zap_create (void *zctx, flux_reactor_t *r)
{
    struct zmqutil_zap *zap;

    if (!r || !zctx) {
        errno = EINVAL;
        return NULL;
    }
    if (!(zap = calloc (1, sizeof (*zap))))
        return NULL;
    if (!(zap->certstore = zhashx_new ())) {
        errno = EINVAL;
        goto error;
    }
    zhashx_set_key_duplicator (zap->certstore, NULL);
    zhashx_set_key_destructor (zap->certstore, NULL);
    zhashx_set_destructor (zap->certstore, cert_destructor);
    if (!(zap->sock = zmq_socket (zctx, ZMQ_REP)))
        goto error;
    if (zmq_bind (zap->sock, ZAP_ENDPOINT) < 0)
        goto error;
    if (!(zap->w = zmqutil_watcher_create (r,
                                           zap->sock,
                                           FLUX_POLLIN,
                                           zap_cb,
                                           zap)))
        goto error;
    flux_watcher_start (zap->w);
    return zap;
error:
    zmqutil_zap_destroy (zap);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
