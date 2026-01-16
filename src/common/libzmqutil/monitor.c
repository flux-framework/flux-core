/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <zmq.h>
#include <stdint.h>
#include <uuid.h>

#include "ccan/array_size/array_size.h"

#include "zwatcher.h"
#include "sockopt.h"
#include "monitor.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

#if (ZMQ_VERSION > ZMQ_MAKE_VERSION (4, 1, 4))
/* N.B. 4.1.4 has a bad bug
 */
struct zmqutil_monitor {
    void *sock;
    char endpoint[UUID_STR_LEN + 64];
    flux_watcher_t *w;
    zmqutil_monitor_f fun;
    void *arg;
    bool stopped;
};

static struct {
    uint16_t event;
    enum {
        VAL_NONE,
        VAL_ERRNO,
        VAL_PROTO,
        VAL_ZAPNUM,
    } valtype;
    const char *desc;
} nametab[] = {
    { ZMQ_EVENT_CONNECTED, VAL_NONE,"connected" },
    { ZMQ_EVENT_CONNECT_DELAYED, VAL_NONE, "connect delayed" },
    { ZMQ_EVENT_CONNECT_RETRIED, VAL_NONE, "connect retried" },
    { ZMQ_EVENT_LISTENING, VAL_NONE, "listening" },
    { ZMQ_EVENT_BIND_FAILED, VAL_ERRNO, "bind failed" },
    { ZMQ_EVENT_ACCEPTED, VAL_NONE, "accepted" },
    { ZMQ_EVENT_ACCEPT_FAILED, VAL_ERRNO, "accept failed" },
    { ZMQ_EVENT_CLOSED, VAL_NONE, "closed" },
    { ZMQ_EVENT_CLOSE_FAILED, VAL_ERRNO, "close failed" },
    { ZMQ_EVENT_DISCONNECTED, VAL_NONE, "disconnected" },
    { ZMQ_EVENT_MONITOR_STOPPED, VAL_NONE, "monitor stopped" },
#ifdef ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL
    { ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL, VAL_ERRNO, "handshake failed" },
#endif
#ifdef ZMQ_EVENT_HANDSHAKE_SUCCEEDED
    { ZMQ_EVENT_HANDSHAKE_SUCCEEDED, VAL_NONE, "handshake succeeded" },
#endif
#ifdef ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL
    { ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL, VAL_PROTO,
      "handshake failed protocol" },
#endif
#ifdef ZMQ_EVENT_HANDSHAKE_FAILED_AUTH
    { ZMQ_EVENT_HANDSHAKE_FAILED_AUTH, VAL_ZAPNUM,
      "handshake failed auth" },
#endif
};

static struct {
    uint32_t value;
    const char *desc;
} prototab[] = {
#ifdef ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL
    { ZMQ_PROTOCOL_ERROR_ZMTP_UNSPECIFIED, "ZMTP unspecified" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_UNEXPECTED_COMMAND, "ZMTP unexpected command" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_INVALID_SEQUENCE, "ZMTP invalid sequence" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_KEY_EXCHANGE, "ZMTP key exchange" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_UNSPECIFIED,
      "ZMTP malformed command unspecified" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_MESSAGE,
      "ZMTP malformed command message" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_HELLO,
      "ZMTP malformed command hello" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_INITIATE,
      "ZMTP malformed command initiate" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_ERROR,
      "ZMTP malformed command error" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_READY,
      "ZMTP malformed command ready" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MALFORMED_COMMAND_WELCOME,
      "ZMTP malformed command welcome" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_INVALID_METADATA, "ZMTP invalid metadata" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_CRYPTOGRAPHIC, "ZMTP cryptographic error" },
    { ZMQ_PROTOCOL_ERROR_ZMTP_MECHANISM_MISMATCH, "ZMTP mechanism mismatch" },
    { ZMQ_PROTOCOL_ERROR_ZAP_UNSPECIFIED, "ZAP unspecified" },
    { ZMQ_PROTOCOL_ERROR_ZAP_MALFORMED_REPLY, "ZAP malformed reply" },
    { ZMQ_PROTOCOL_ERROR_ZAP_BAD_REQUEST_ID, "ZAP bad request id" },
    { ZMQ_PROTOCOL_ERROR_ZAP_BAD_VERSION, "ZAP bad version" },
    { ZMQ_PROTOCOL_ERROR_ZAP_INVALID_STATUS_CODE, "ZAP invalid status code" },
    { ZMQ_PROTOCOL_ERROR_ZAP_INVALID_METADATA, "ZAP invalid metadata" },
#endif
};

static const char *eventstr (struct monitor_event *mevent)
{
    int i;
    for (i = 0; i < ARRAY_SIZE (nametab); i++) {
        if (mevent->event == nametab[i].event)
            return nametab[i].desc;
    }
    return "unknown socket event";
}

static const char *valuestr (struct monitor_event *mevent)
{
    int i;
    int valtype = VAL_NONE;
    static char buf[128];

    /* look up valtype for event */
    for (i = 0; i < ARRAY_SIZE (nametab); i++) {
        if (mevent->event == nametab[i].event)
            valtype = nametab[i].valtype;
    }
    /* decode value depending on valtype */
    if (valtype == VAL_ERRNO)
        return strerror (mevent->value);
    if (valtype == VAL_ZAPNUM) {
        snprintf (buf, sizeof (buf), "ZAP status code %d", mevent->value);
        return buf;
    }
    if (valtype == VAL_PROTO) {
        for (i = 0; i < ARRAY_SIZE (prototab); i++) {
            if (mevent->value == prototab[i].value)
                return prototab[i].desc;
        }
        snprintf (buf, sizeof (buf), "unknown protocol error %d", mevent->value);
        return buf;
    }
    return "";
}

bool zmqutil_monitor_iserror (struct monitor_event *mevent)
{
    if (mevent) {
        switch (mevent->event) {
            case ZMQ_EVENT_ACCEPT_FAILED:
            case ZMQ_EVENT_CLOSE_FAILED:
#ifdef ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL
            case ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
#endif
#ifdef ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL
            case ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL:
#endif
#ifdef ZMQ_EVENT_HANDSHAKE_FAILED_AUTH
            case ZMQ_EVENT_HANDSHAKE_FAILED_AUTH:
#endif
                return true;
        }
    }
    return false;
}

static int recv_frame1 (void *sock, uint16_t *event, uint32_t *value)
{
    uint8_t buf[6];
    if (zmq_recv (sock, buf, sizeof (buf), 0) != 6)
        return -1;
    *event = *(uint16_t *)buf;
    *value = *(uint32_t *)(buf + 2);
    return 0;
}

static int recv_frame2 (void *sock, char *buf, int size)
{
    int more;
    if (zgetsockopt_int (sock, ZMQ_RCVMORE, &more) < 0 || !more)
        return -1;
    memset (buf, 0, size);
    if (zmq_recv (sock, buf, size, 0) < 0)
        return -1;
    return 0;
}

int zmqutil_monitor_get (struct zmqutil_monitor *mon,
                         struct monitor_event *mevent)
{
    if (!mon || !mevent) {
        errno = EINVAL;
        return -1;
    }
    /* receive event+value frame */
    if (recv_frame1 (mon->sock, &mevent->event, &mevent->value) < 0)
        return -1;

    /* receive endpoint frame */
    if (recv_frame2 (mon->sock,
                     mevent->endpoint,
                     sizeof (mevent->endpoint)) < 0)
        return -1;

    /* note end of monitor stream for zmqutil_monitor_destroy() */
    if (mevent->event == ZMQ_EVENT_MONITOR_STOPPED)
        mon->stopped = true;

    /* decode event, value */
    mevent->event_str = eventstr (mevent);
    mevent->value_str = valuestr (mevent);
    return 0;
}

static void monitor_callback (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct zmqutil_monitor *mon = arg;

    if (mon->fun)
        mon->fun (mon, mon->arg);
}

/* Read messages from the monitor socket until the final MONITOR_STOPPED
 * message is read.  This presumes that the socket being monitored is
 * closed before zmqutil_monitor_destroy() is called.  If the monitor socket
 * is destroyed before consuming this event, it may cause the 0MQ I/O thread
 * to block when the PAIR socket it is writing to enters the mute state.
 */
static void monitor_purge (struct zmqutil_monitor *mon)
{
    struct monitor_event event;

    while (!mon->stopped) {
        if (zmqutil_monitor_get (mon, &event) < 0)
            break;
    }
}

void zmqutil_monitor_destroy (struct zmqutil_monitor *mon)
{
    if (mon) {
        int saved_errno = errno;
        flux_watcher_destroy (mon->w);
        if (mon->sock) {
            monitor_purge (mon);
            (void)zmq_disconnect (mon->sock, mon->endpoint);
            (void)zmq_close (mon->sock);
        }
        free (mon);
        errno = saved_errno;
    }
}

struct zmqutil_monitor *zmqutil_monitor_create (void *zctx,
                                                void *sock,
                                                flux_reactor_t *r,
                                                zmqutil_monitor_f fun,
                                                void *arg)
{
    struct zmqutil_monitor *mon;
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    if (!zctx || !sock || !r) {
        errno = EINVAL;
        return NULL;
    }
    if (!(mon = calloc (1, sizeof (*mon))))
        return NULL;
    mon->fun = fun;
    mon->arg = arg;

    /* Generate a unique inproc endpoint for monitoring this socket.
     */
    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);
    snprintf (mon->endpoint, sizeof (mon->endpoint), "inproc://%s", uuid_str);

    /* Arrange for local callback to run on each monitor event.
     * It will call the user's callback.
     */
    if (zmq_socket_monitor (sock,
                            mon->endpoint,
                            ZMQ_EVENT_ALL) < 0
        || !(mon->sock = zmq_socket (zctx, ZMQ_PAIR))
        || zmq_connect (mon->sock, mon->endpoint) < 0
        || !(mon->w = zmqutil_watcher_create (r,
                                              mon->sock,
                                              FLUX_POLLIN,
                                              monitor_callback,
                                              mon)))
        goto error;
    if (zsetsockopt_int (mon->sock, ZMQ_LINGER, 0) < 0
        || zsetsockopt_int (mon->sock, ZMQ_RCVHWM, 0) < 0
        || zsetsockopt_int (mon->sock, ZMQ_SNDHWM, 0) < 0)
        goto error;
    flux_watcher_start (mon->w);
    return mon;
error:
    zmqutil_monitor_destroy (mon);
    return NULL;
}
#else
/* Monitoring is disabled due to libzmq being too old.
 */
struct zmqutil_monitor *zmqutil_monitor_create (void *zctx,
                                                void *sock,
                                                flux_reactor_t *r,
                                                zmqutil_monitor_f fun,
                                                void *arg)
{
    return NULL;
}
void zmqutil_monitor_destroy (struct zmqutil_monitor *mon)
{
}
int zmqutil_monitor_get (struct zmqutil_monitor *mon,
                         struct monitor_event *mevent)
{
    return -1;
}
bool zmqutil_monitor_iserror (struct monitor_event *mevent)
{
    return false;
}
#endif

// vi:ts=4 sw=4 expandtab
