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

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <flux/core.h>

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
    struct addrinfo *addrinfo;
    int sock;
    int buf_len;
    int tail;
    char *buf;
    char prefix[INTERNAL_BUFFSIZE];

    zhashx_t *metrics;
    zlist_t *done;
    flux_watcher_t *w;
    double period;

    bool enabled;
};

bool fripp_enabled (struct fripp_ctx *ctx)
{
    if (ctx && ctx->enabled)
        return true;
    return false;
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
    int len;
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
        (void)sendto (ctx->sock,
                      packet,
                      len,
                      0,
                      ctx->addrinfo->ai_addr,
                      ctx->addrinfo->ai_addrlen);
    } while (split && (packet = &packet[len + 1]));

    return 0;
}

int fripp_packet_appendf (struct fripp_ctx *ctx, const char *fmt, ...)
{
    va_list ap, cpy;
    char buf[INTERNAL_BUFFSIZE];
    int len, rc = 0;

    if (!fripp_enabled (ctx))
        return 0;

    va_start (ap, fmt);
    va_copy (cpy, ap);

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

    if (!fripp_enabled (ctx))
        return 0;

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
    if (!fripp_enabled (ctx))
        return 0;

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
    if (!fripp_enabled (ctx))
        return 0;

    if (ctx->period == 0)
        return fripp_sendf (ctx,
                            "%s.%s:%s%zd|g\n",
                            ctx->prefix,
                            name,
                            inc && value > 0 ? "+" : "",
                            value);

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
    if (!fripp_enabled (ctx))
        return 0;

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

    if (!fripp_enabled (ctx)) {
        flux_watcher_stop (w);
        return;
    }

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
    if (!fripp_enabled (ctx))
        return;

    if (period <= 0) {
        flux_watcher_stop (ctx->w);
        ctx->period = 0;
        return;
    }

    ctx->period = period;

    double after = flux_watcher_next_wakeup (ctx->w) - (double) time (NULL);

    flux_timer_watcher_reset (ctx->w, after, ctx->period);
}

void fripp_ctx_destroy (struct fripp_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_watcher_destroy (ctx->w);
        zhashx_destroy (&ctx->metrics);
        zlist_destroy (&ctx->done);
        if (ctx->sock != -1)
            close (ctx->sock);
        free (ctx->buf);
        if (ctx->addrinfo)
            freeaddrinfo (ctx->addrinfo);
        free (ctx);
        errno = saved_errno;
    }
}

static int parse_address (struct addrinfo **aip,
                          const char *s,
                          char *errbuf,
                          int errbufsz)
{
    char *node;
    char *service;
    struct addrinfo hints, *res = NULL;
    int e;

    if (!(node = strdup (s))) {
        snprintf (errbuf, errbufsz, "out of memory");
        return -1;
    }
    if (!(service = strchr (node, ':'))) {
        snprintf (errbuf, errbufsz, "missing colon delimiter");
        goto error;
    }
    *service++ = '\0';

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if ((e = getaddrinfo (node, service, &hints, &res)) != 0) {
        snprintf (errbuf, errbufsz, "%s", gai_strerror (e));
        goto error;
    }
    free (node);
    *aip = res;
    return 0;
error:
    if (res)
        freeaddrinfo (res);
    free (node);
    errno = EINVAL;
    return -1;
}

struct fripp_ctx *fripp_ctx_create (flux_t *h)
{
    struct fripp_ctx *ctx;
    char *addr;
    char errbuf[128];

    if (!(ctx = calloc (1, sizeof (*ctx))))
        goto error;
    ctx->sock = -1;
    /* If environment variable is unset, or it is set wrong, let the context
     * be created with disabled status so we don't repeatedly try to create
     * it "on demand" in the flux_stats_*() functions.
     */
    if (!(addr = getenv ("FLUX_FRIPP_STATSD")))
        return ctx;
    if (parse_address (&ctx->addrinfo, addr, errbuf, sizeof (errbuf)) < 0) {
        flux_log (h, LOG_ERR, "FLUX_FRIPP_STATSD parse error: %s", errbuf);
        return ctx;
    }
    if ((ctx->sock = socket (ctx->addrinfo->ai_family,
                             ctx->addrinfo->ai_socktype,
                             ctx->addrinfo->ai_protocol)) == -1)
        goto error;
    if (!(ctx->buf = calloc (1, INTERNAL_BUFFSIZE)))
        goto error;

    ctx->buf_len = INTERNAL_BUFFSIZE;
    ctx->tail = 0;

    uint32_t rank;
    char buf[16];
    flux_get_rank (h, &rank);
    sprintf (buf, "flux.%d", rank);
    fripp_set_prefix (ctx, buf);

    if (!(ctx->metrics = zhashx_new ()))
        goto nomem;
    zhashx_set_destructor (ctx->metrics, metric_destroy);

    if (!(ctx->done = zlist_new ()))
        goto nomem;
    if (!(ctx->w = flux_timer_watcher_create (
              flux_get_reactor(h),
              DEFAULT_AGG_PERIOD,
              DEFAULT_AGG_PERIOD,
              timer_cb,
              ctx)))
        goto error;

    ctx->period = DEFAULT_AGG_PERIOD;
    flux_watcher_start (ctx->w);

    ctx->enabled = true;

    return ctx;
nomem:
    errno = ENOMEM;
error:
    fripp_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
