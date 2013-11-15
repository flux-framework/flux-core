/* logcli.c - log client */

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "util.h"
#include "flux.h"

typedef struct {
    char *facility; 
} logctx_t;

static void freectx (logctx_t *ctx)
{
    if (ctx->facility)
        free (ctx->facility);
    free (ctx);
}

static logctx_t *getctx (flux_t h)
{
    logctx_t *ctx = (logctx_t *)flux_aux_get (h, "logcli");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->facility = xstrdup ("unknown");
        flux_aux_set (h, "logcli", ctx, (FluxFreeFn)freectx);
    }
    
    return ctx;
}

static json_object *log_create (int level, const char *fac, const char *src,
                                const char *fmt, va_list ap)
{
    json_object *o = util_json_object_new_object ();
    char *str = NULL;
    struct timeval tv;

    if (gettimeofday (&tv, NULL) < 0)
        err_exit ("gettimeofday");

    if (vasprintf (&str, fmt, ap) < 0)
        oom ();
    if (strlen (str) == 0) {
        errno = EINVAL;
        goto error;
    }
    util_json_object_add_int (o, "count", 1);
    util_json_object_add_string (o, "facility", fac);
    util_json_object_add_int (o, "level", level);
    util_json_object_add_string (o, "source", src);
    util_json_object_add_timeval (o, "timestamp", &tv);
    util_json_object_add_string (o, "message", str);
    free (str);
    return o;
error:
    if (str)
        free (str);
    json_object_put (o);
    return NULL;
}

void flux_log_set_facility (flux_t h, const char *facility)
{
    logctx_t *ctx = getctx (h);

    if (ctx->facility)
        free (ctx->facility);
    ctx->facility = xstrdup (facility);
}

int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap)
{
    logctx_t *ctx = getctx (h);
    json_object *request = NULL;
    char src[8];
    int rc = -1;

    snprintf (src, sizeof (src), "%d", flux_rank (h));
    if (!(request = log_create (lev, ctx->facility, src, fmt, ap)))
        goto done;
    if (flux_request_send (h, request, "log.msg") < 0)
        goto done;
    rc = 0;
done:
    if (request)
        json_object_put (request);
    return rc;
}

int flux_log (flux_t h, int lev, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_vlog (h, lev, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_log_subscribe (flux_t h, int lev, const char *sub)
{
    return flux_request_send (h, NULL, "log.subscribe.%d.%s", lev, sub);
}

int flux_log_unsubscribe (flux_t h, const char *sub)
{
    return flux_request_send (h, NULL, "log.unsubscribe.%s", sub);
}

int flux_log_dump (flux_t h, int lev, const char *sub)
{
    return flux_request_send (h, NULL, "log.dump.%d.%s", lev, sub);
}

char *flux_log_decode (zmsg_t *zmsg, int *lp, char **fp, int *cp,
                    struct timeval *tvp, char **sp)
{
    json_object *response = NULL;
    const char *s, *fac, *src;
    char *msg = NULL;
    int lev, count;
    struct timeval tv;

    if (cmb_msg_decode (zmsg, NULL, &response) < 0)
        goto done;
    if (!response) {
        errno = EPROTO;
        goto done;
    }
    if (util_json_object_get_int (response, "errnum", &errno) == 0)
        goto done;
    if (util_json_object_get_string (response, "facility", &fac) < 0
     || util_json_object_get_int (response, "level", &lev) < 0
     || util_json_object_get_string (response, "source", &src) < 0
     || util_json_object_get_timeval (response, "timestamp", &tv) < 0
     || util_json_object_get_string (response, "message", &s) < 0
     || util_json_object_get_int (response, "count", &count) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (tvp)
        *tvp = tv;
    if (lp)
        *lp = lev;
    if (fp)
        *fp = xstrdup (fac);
    if (cp)
        *cp = count;
    if (sp)
        *sp = xstrdup (src); 
    msg = xstrdup (s);
done:
    if (response)
        json_object_put (response);
    return msg;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
