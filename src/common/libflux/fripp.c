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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "attr.h"
#include "flog.h"
#include "fripp.h"
#include "reactor.h"

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/iterators.h"

#define FRIPP_MAX_PACKET_LEN 1440
#define INTERNAL_BUFFSIZE 128
#define DEFAULT_AGG_PERIOD 1.0

typedef enum {
    BRUBECK_COUNTER,
    BRUBECK_GAUGE,
    BRUBECK_TIMER
} metric_type;

union val {
    ssize_t l;
    double d;
};

struct metric {
    union val cur;
    union val prev;
    bool inc;
    metric_type type;
};

struct fripp_ctx {
    struct sockaddr_in si_server;
    int sock;
    int buf_len;
    int tail;
    char *buf;
    char prefix[INTERNAL_BUFFSIZE];

    zhashx_t *metrics;
    zlist_t *done;
    flux_watcher_t *w;
    double period;
};

bool fripp_enabled (struct fripp_ctx *ctx, const char *metric)
{
    if (ctx && metric)
        return zhashx_lookup (ctx->metrics, metric) != NULL;

    return getenv ("FLUX_FRIPP_STATSD") != NULL;
}

void fripp_set_prefix (struct fripp_ctx *ctx, const char *prefix)
{
    strncpy (ctx->prefix, prefix, INTERNAL_BUFFSIZE - 1);
}

static int split_packet (char *packet, int len)
{
    for (int i = len - 1; i >= 0; --i) {
        if (packet[i] == '\n' || packet[i] == '\0') {
            packet[i] = '\0';
            return i;
        }
    }

    return -1;
}

static int fripp_send_metrics (struct fripp_ctx *ctx)
{
    int len, sock_len = sizeof (ctx->si_server);
    bool split;
    char *packet = ctx->buf;

    do {
        split = false;
        len = strlen (packet);

        if (len > FRIPP_MAX_PACKET_LEN)
            split = true;
        if ((len = split_packet (packet, len > FRIPP_MAX_PACKET_LEN ?
                FRIPP_MAX_PACKET_LEN : len)) == -1)
            return -1;
        (void) sendto (ctx->sock, packet, len, 0,
                    (void *) &ctx->si_server, sock_len);
    } while (split && (packet = &packet[len + 1]));

    return 0;
}

int fripp_packet_appendf (struct fripp_ctx *ctx, const char *fmt, ...)
{
    va_list ap, cpy;
    va_start (ap, fmt);
    va_copy (cpy, ap);

    char buf[INTERNAL_BUFFSIZE];
    int len, rc = 0;

    if ((len = vsnprintf (buf, INTERNAL_BUFFSIZE, fmt, ap))
            >= INTERNAL_BUFFSIZE) {
        rc = -1;
        goto done;
    }

    ctx->tail += len;

    if (ctx->tail >= ctx->buf_len) {
        char *tmp;
        if (!(tmp = realloc(ctx->buf, (ctx->tail + 1) * sizeof (char)))) {
            rc = -1;
            goto done;
        }

        ctx->buf_len = ctx->tail + 1;
        ctx->buf = tmp;
    }

    strcat (ctx->buf, buf);

done:
    va_end (ap);
    va_end (cpy);

    return rc;
}

int fripp_sendf (struct fripp_ctx *ctx, const char *fmt, ...)
{
    va_list ap, cpy;
    va_start (ap, fmt);
    va_copy (cpy, ap);

    int len, rc = 0;

    if ((len = vsnprintf (ctx->buf, ctx->buf_len, fmt, ap))
         >= ctx->buf_len) {

        char *tmp;
        if (!(tmp = realloc (ctx->buf, (len + 1) * sizeof (char)))) {
            rc = -1;
            goto done;
        }

        ctx->buf = tmp;
        ctx->buf_len = len + 1;
        (void) vsnprintf (ctx->buf, ctx->buf_len, fmt, cpy);
    }

    rc = fripp_send_metrics (ctx);

done:
    va_end (ap);
    va_end (cpy);

    return rc;
}

int fripp_count (struct fripp_ctx *ctx,
                 const char *name,
                 ssize_t count)
{
    if (ctx->period == 0)
        return fripp_sendf (ctx, "%s.%s:%zd|C\n", ctx->prefix, name, count);

    struct metric *m;

    if (!(m = zhashx_lookup (ctx->metrics, name))) {
        if (!(m = malloc (sizeof (*m))))
            return -1;

        zhashx_insert (ctx->metrics, name, (void *) m);
        m->prev.l = -1;
    }

    m->type = BRUBECK_COUNTER;
    m->inc = false;
    m->cur.l = count;

    flux_watcher_start (ctx->w);

    return 0;
}

int fripp_gauge (struct fripp_ctx *ctx,
                 const char *name,
                 ssize_t value,
                 bool inc)
{
    if (ctx->period == 0)
        return fripp_sendf (ctx, inc && value > 0 ?
                            "%s.%s:+%zd|g\n" : "%s.%s:%zd|g\n",
                            ctx->prefix, name, value);

    struct metric *m;

    if (!(m = zhashx_lookup (ctx->metrics, name))) {
        if (!(m = calloc (1, sizeof (*m))))
            return -1;

        zhashx_insert (ctx->metrics, name, (void *) m);
        m->prev.l = -1;
    }

    m->type = BRUBECK_GAUGE;
    m->inc = inc;
    m->cur.l = inc ? m->cur.l + value : value;

    flux_watcher_start (ctx->w);

    return 0;
}

int fripp_timing (struct fripp_ctx *ctx,
                  const char *name,
                  double ms)
{
    if (ctx->period == 0)
        return fripp_sendf (ctx, "%s.%s:%lf|ms\n", ctx->prefix, name, ms);

    struct metric *m;

    if (!(m = zhashx_lookup (ctx->metrics, name))) {
        if (!(m = malloc (sizeof (*m))))
            return -1;

        zhashx_insert (ctx->metrics, name, (void *) m);
        m->prev.d = -1.0;
    }

    m->type = BRUBECK_TIMER;
    m->inc = false;
    m->cur.d = ms;

    flux_watcher_start (ctx->w);

    return 0;
}

static void metric_destroy (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct fripp_ctx *ctx = arg;
    const char *name;
    struct metric *m;

    int rc = 0;

    if (!ctx->buf[0] && zhashx_size (ctx->metrics) == 0) {
        flux_watcher_stop (ctx->w);
        return;
    }

    FOREACH_ZHASHX (ctx->metrics, name, m) {
        if ((m->type == BRUBECK_COUNTER || m->type == BRUBECK_GAUGE)
                && m->cur.l == m->prev.l) {
            zlist_append (ctx->done, (void *) name);
            continue;
        }
        if (m->type == BRUBECK_TIMER && m->cur.d == m->prev.d) {
            zlist_append (ctx->done, (void *) name);
            continue;
        }

        m->prev.l = m->cur.l;

        switch (m->type) {
            case BRUBECK_COUNTER:
                rc = fripp_packet_appendf (ctx,
                                           "%s.%s:%zd|C\n",
                                           ctx->prefix,
                                           name,
                                           m->cur.l);
                break;
            case BRUBECK_GAUGE:
                rc = fripp_packet_appendf (ctx,
                                           "%s.%s:%zd|g\n",
                                           ctx->prefix,
                                           name,
                                           m->cur.l);
                break;
            case BRUBECK_TIMER:
                rc = fripp_packet_appendf (ctx,
                                           "%s.%s:%lf|ms\n",
                                           ctx->prefix,
                                           name,
                                           m->cur.d);
                break;
            default:
                break;
        }

        if (rc)
            continue;
    }

    fripp_send_metrics (ctx);

    char *s;
    while ((s = zlist_pop (ctx->done)))
        zhashx_delete (ctx->metrics, s);

    memset (ctx->buf, 0, ctx->buf_len);

    ctx->tail = 0;
}

void fripp_set_agg_period (struct fripp_ctx *ctx, double period)
{
    if (period <= 0) {
        flux_watcher_stop (ctx->w);
        ctx->period = 0;
        return;
    }

    ctx->period = period;
    double after = flux_watcher_next_wakeup (ctx->w) - (double) time (NULL);

    flux_timer_watcher_reset (ctx->w, after, ctx->period);
}

void fripp_ctx_destroy (void *arg)
{
    struct fripp_ctx *ctx = arg;

    if (ctx->w) {
        flux_watcher_stop (ctx->w);
        flux_watcher_destroy (ctx->w);
    }
    if (ctx->metrics) {
        zhashx_purge (ctx->metrics);
        zhashx_destroy (&ctx->metrics);
    }
    if (ctx->done) {
        zlist_destroy (&ctx->done);
    }
    close (ctx->sock);
    free (ctx->buf);
    free (ctx);
}

struct fripp_ctx *fripp_ctx_create (flux_t *h)
{
    struct fripp_ctx *ctx;
    char *uri, *port_s = NULL, *host = NULL;

    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        flux_log_error (h, "fripp_ctx_create");
        goto error;
    }
    if (!(uri = getenv ("FLUX_FRIPP_STATSD"))) {
        flux_log_error (h, "FLUX_FRIPP_STATSD env var not set");
        goto error;
    }
    if (!(port_s = strrchr (uri, ':'))) {
        flux_log_error (h, "FLUX_FRIPP_STATSD env var no port");
        goto error;
    }
    if (!(host = calloc (1, port_s - uri + 1))) {
        flux_log_error (h, "fripp_ctx_create");
        goto error;
    }

    strncpy (host, uri, port_s - uri);
    uint16_t port = (uint16_t) atoi (++port_s);

    memset (&ctx->si_server, 0, sizeof (ctx->si_server));
    ctx->si_server.sin_family = AF_INET;
    ctx->si_server.sin_port = htons (port);

    if (!inet_aton (host, &ctx->si_server.sin_addr)) {
        flux_log_error (h, "error creating server address");
        free (host);
        goto error;
    }

    free (host);

    if ((ctx->sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        flux_log_error (h, "error opening socket");
        goto error;
    }
    if (!(ctx->buf = calloc (1, INTERNAL_BUFFSIZE))) {
        flux_log_error (h, "calloc");
        close (ctx->sock);
        goto error;
    }

    ctx->buf_len = INTERNAL_BUFFSIZE;
    ctx->tail = 0;

    uint32_t rank;
    char buf[16];
    flux_get_rank (h, &rank);
    sprintf (buf, "flux.%d", rank);
    fripp_set_prefix (ctx, buf);

    if (!(ctx->metrics = zhashx_new ())) {
        fripp_ctx_destroy (ctx);
        return NULL;
    }

    zhashx_set_destructor (ctx->metrics, metric_destroy);


    if (!(ctx->done = zlist_new ())) {
        fripp_ctx_destroy (ctx);
        return NULL;
    }
    if (!(ctx->w = flux_timer_watcher_create (
              flux_get_reactor(h),
              DEFAULT_AGG_PERIOD,
              DEFAULT_AGG_PERIOD,
              timer_cb,
              ctx))) {
        fripp_ctx_destroy (ctx);
        return NULL;
    }


    ctx->period = DEFAULT_AGG_PERIOD;
    flux_watcher_start (ctx->w);

    return ctx;

error:
    if (ctx)
        free (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
