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
#include <czmq.h>
#include <zmq.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "reactor.h"
#include "sockopt.h"
#include "mpart.h"
#include "zap.h"

#define ZAP_ENDPOINT "inproc://zeromq.zap.01"

struct zmqutil_zap {
    zcertstore_t *certstore;
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
    zcert_t *cert;
    const char *name = NULL;
    int log_level = LOG_ERR;

    if ((req = mpart_recv (zap->sock))) {
        if (!mpart_streq (req, 0, "1.0")
            || !mpart_streq (req, 5, "CURVE")
            || get_mpart_pubkey (req, 6, pubkey) < 0) {
            logger (zap, LOG_ERR, "ZAP request decode error");
            goto done;
        }
        if ((cert = zcertstore_lookup (zap->certstore, pubkey)) != NULL) {
            status_code = "200";
            status_text = "OK";
            user_id = pubkey;
            name = zcert_meta (cert, "name");
            log_level = LOG_DEBUG;
        }
        if (!name)
            name = "unknown";
        logger (zap,
                log_level,
                "overlay auth cert-name=%s %s",
                name,
                status_text);

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

/* Create a zcert_t and add it to in-memory zcertstore_t.
 */
int zmqutil_zap_authorize (struct zmqutil_zap *zap,
                           const char *name,
                           const char *pubkey)
{
    uint8_t public_key[32];
    zcert_t *cert;

    if (!zap
        || !name
        || !pubkey
        || strlen (pubkey) != 40
        || !zmq_z85_decode (public_key, pubkey)) {
        errno = EINVAL;
        return -1;
    }
    if (!(cert = zcert_new_from (public_key, public_key))) {
        errno = ENOMEM;
        return -1;
    }
    zcert_set_meta (cert, "name", "%s", name);
    zcertstore_insert (zap->certstore, &cert); // takes ownership of cert
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
        zcertstore_destroy (&zap->certstore);
        free (zap);
        errno = saved_errno;
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
    if (!(zap->certstore = zcertstore_new (NULL)))
        goto error;
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
