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
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <zmq.h>

#include "reactor.h"
#include "zap.h"

#define ZAP_ENDPOINT "inproc://zeromq.zap.01"

struct zmqutil_zap {
    zcertstore_t *certstore;
    void *sock;
    flux_watcher_t *w;
    zaplog_f logger;
    void *logger_arg;
};

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
    zmsg_t *req = NULL;
    zmsg_t *rep = NULL;
    char pubkey[41];
    const char *status_code = "400";
    const char *status_text = "No access";
    const char *user_id = "";
    zcert_t *cert;
    const char *name = NULL;
    int log_level = LOG_ERR;

    if ((req = zmsg_recv (zap->sock))) {
        if (!streq_zmsg_nth (req, 0, "1.0")
                || !streq_zmsg_nth (req, 5, "CURVE")
                || !pubkey_zmsg_nth (req, 6, pubkey)) {
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

        if (!(rep = zmsg_new ()))
            goto done;
        if (!add_zmsg_nth (rep, req, 0)
                || !add_zmsg_nth (rep, req, 1)
                || zmsg_addstr (rep, status_code) < 0
                || zmsg_addstr (rep, status_text) < 0
                || zmsg_addstr (rep, user_id) < 0
                || zmsg_addmem (rep, NULL, 0) < 0) {
            logger (zap, LOG_ERR, "ZAP response encode error");
            goto done;
        }
        if (zmsg_send (&rep, zap->sock) < 0)
            logger (zap, LOG_ERR, "ZAP send error");
    }
done:
    zmsg_destroy (&req);
    zmsg_destroy (&rep);
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
