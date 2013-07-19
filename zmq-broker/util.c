#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <json/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <math.h>
#include <limits.h>
#include <openssl/evp.h>
#include <assert.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "hostlist.h"

void oom (void)
{
    errno = ENOMEM;
    msg_exit ("out of memory");
    exit (1);
}

void *xzmalloc (size_t size)
{
    void *new;

    new = malloc (size);
    if (!new)
        oom ();
    memset (new, 0, size);
    return new;
}

char *xstrdup (const char *s)
{
    char *cpy = strdup (s);
    if (!cpy)
        oom ();
    return cpy;
}

void xgettimeofday (struct timeval *tv, struct timezone *tz)
{
    if (gettimeofday (tv, tz) < 0)
        err_exit ("gettimeofday");
}

int env_getint (char *name, int dflt)
{
    char *ev = getenv (name);
    return ev ? strtoul (ev, NULL, 10) : dflt;
}

char *env_getstr (char *name, char *dflt)
{
    char *ev = getenv (name);
    return ev ? xstrdup (ev) : xstrdup (dflt);
}

static int _strtoia (char *s, int *ia, int ia_len)
{
    char *next;
    int n, len = 0;

    while (*s) {
        n = strtoul (s, &next, 10);
        s = *next == '\0' ? next : next + 1;
        if (ia) {
            if (ia_len == len)
                break;
            ia[len] = n;
        }
        len++;
    }
    return len;
}

int getints (char *s, int **iap, int *lenp)
{
    int len = _strtoia (s, NULL, 0);
    int *ia = malloc (len * sizeof (int));

    if (!ia)
        return -1;

    (void)_strtoia (s, ia, len);
    *lenp = len;
    *iap = ia;
    return 0;
}

int env_getints (char *name, int **iap, int *lenp, int dflt_ia[], int dflt_len)
{
    char *s = getenv (name);
    int *ia;
    int len;

    if (s) {
        if (getints (s, &ia, &len) < 0)
            return -1;
    } else {
        ia = malloc (dflt_len * sizeof (int));
        if (!ia)
            return -1;
        for (len = 0; len < dflt_len; len++)
            ia[len] = dflt_ia[len];
    }
    *lenp = len;
    *iap = ia;
    return 0;
}

int mapstr (char *s, mapstrfun_t fun, void *arg1, void *arg2)
{
    char *cpy = xstrdup (s);
    char *saveptr, *a1 = cpy;
    int rc = 0;

    for (;;) {
        char *name = strtok_r (a1 , ",", &saveptr);
        if (!name)
            break;
        if (fun (name, arg1, arg2) < 0) {
            rc = -1;
            break;
        }
        a1 = NULL;
    }
    free (cpy);
    return rc;
}

char *argv_concat (int argc, char *argv[])
{
    int i, len = 0;
    char *s;

    for (i = 0; i < argc; i++)
        len += strlen (argv[i]) + 1;
    s = xzmalloc (len + 1);
    for (i = 0; i < argc; i++) {
        strcat (s, argv[i]);
        if (i < argc - 1)
            strcat (s, " "); 
    }
    return s; 
}

void util_json_object_add_boolean (json_object *o, char *name, bool val)
{
    json_object *no;

    if (!(no = json_object_new_boolean (val)))
        oom ();
    json_object_object_add (o, name, no);
}

void util_json_object_add_double (json_object *o, char *name, double n)
{
    json_object *no;

    if (!(no = json_object_new_double (n)))
        oom ();
    json_object_object_add (o, name, no);
}

void util_json_object_add_int (json_object *o, char *name, int i)
{
    json_object *no;

    if (!(no = json_object_new_int (i)))
        oom ();
    json_object_object_add (o, name, no);
}

void util_json_object_add_string (json_object *o, char *name, const char *s)
{
    json_object *no;

    if (!(no = json_object_new_string (s)))
        oom ();
    json_object_object_add (o, name, no);
}

void util_json_object_add_base64 (json_object *o, char *name,
                                  uint8_t *dat, int len)
{
    EVP_ENCODE_CTX ectx;
    size_t size = len*2;
    uint8_t *out;
    int outlen = 0;
    int tlen = 0;

    if (size < 64)
        size = 64;
    out = xzmalloc (size + 1);
    EVP_EncodeInit (&ectx);
    EVP_EncodeUpdate (&ectx, out, &outlen, dat, len);
    tlen += outlen;
    EVP_EncodeFinal (&ectx, out + tlen, &outlen);
    tlen += outlen;
    assert (tlen < size);
    if (out[tlen - 1] == '\n')
        out[tlen - 1] = '\0';
    util_json_object_add_string (o, name, (char *)out);
    free (out);
}

void util_json_object_add_timeval (json_object *o, char *name,
                                   struct timeval *tvp)
{
    json_object *no;
    char tbuf[32];

    snprintf (tbuf, sizeof (tbuf), "%lu.%lu", tvp->tv_sec, tvp->tv_usec);
    if (!(no = json_object_new_string (tbuf)))
        oom ();
    json_object_object_add (o, name, no);
}

int util_json_object_get_boolean (json_object *o, char *name, bool *vp)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *vp = json_object_get_boolean (no);
    return 0;
}

int util_json_object_get_double (json_object *o, char *name, double *dp)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *dp = json_object_get_double (no);
    return 0;
}

int util_json_object_get_int (json_object *o, char *name, int *ip)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *ip = json_object_get_int (no);
    return 0;
}

int util_json_object_get_string (json_object *o, char *name, const char **sp)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *sp = json_object_get_string (no);
    return 0;
}

int util_json_object_get_base64 (json_object *o, char *name,
                                 uint8_t **datp, int *lenp)
{
    const char *s;
    int slen;
    EVP_ENCODE_CTX ectx;
    uint8_t *out = NULL;
    int outlen = 0;
    int tlen = 0;

    if (util_json_object_get_string (o, name, &s) == 0) {
        slen = strlen (s);
        out = xzmalloc (slen);
        EVP_DecodeInit (&ectx);
        EVP_DecodeUpdate (&ectx, out, &outlen, (uint8_t *)s, slen);
        tlen += outlen;
        EVP_DecodeFinal (&ectx, out + tlen, &outlen);
        tlen += outlen;
    }
    *datp = out;
    *lenp = tlen;    
    return 0;
}

int util_json_object_get_timeval (json_object *o, char *name,
                                  struct timeval *tvp)
{
    struct timeval tv;
    char *endptr;
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    tv.tv_sec = strtoul (json_object_get_string (no), &endptr, 10);
    tv.tv_usec = *endptr ? strtoul (endptr + 1, NULL, 10) : 0;
    *tvp = tv;
    return 0;
}

int util_json_object_get_int_array (json_object *o, char *name,
                                    int **ap, int *lp)
{
    json_object *no = json_object_object_get (o, name);
    json_object *vo;
    int i, len, *arr = NULL;

    if (!no)
        goto error;
    len = json_object_array_length (no);
    arr = xzmalloc (sizeof (int) * len);
    for (i = 0; i < len; i++) {
        vo = json_object_array_get_idx (no, i);
        if (!vo)
            goto error;
        arr[i] = json_object_get_int (vo);
    }
    *ap = arr;
    *lp = len;
    return 0;
error:
    if (arr)
        free (arr);
    return -1;
}

json_object *util_json_object_new_object (void)
{
    json_object *o;

    if (!(o = json_object_new_object ()))
        oom ();
    return o;
}

json_object *util_json_vlog (logpri_t pri, const char *fac, const char *src,
                             const char *fmt, va_list ap)
{
    json_object *o = util_json_object_new_object ();
    char *str = NULL;
    struct timeval tv;

    xgettimeofday (&tv, NULL);

    if (vasprintf (&str, fmt, ap) < 0)
        oom ();
    if (strlen (str) == 0) {
        errno = EINVAL;
        goto error;
    }
    util_json_object_add_int (o, "count", 1);
    util_json_object_add_string (o, "facility", fac);
    util_json_object_add_int (o, "priority", pri);
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

const char *util_logpri_str (logpri_t pri)
{
    switch (pri) {
        case CMB_LOG_EMERG: return "emerg";
        case CMB_LOG_ALERT: return "alert";
        case CMB_LOG_CRIT: return "crit";
        case CMB_LOG_ERR: return "err";
        case CMB_LOG_WARNING: return "warning";
        case CMB_LOG_NOTICE: return "notice";
        case CMB_LOG_INFO: return "info";
        case CMB_LOG_DEBUG: return "debug";
    }
    /*NOTREACHED*/
    return "unknown";
}

int util_logpri_val (const char *p, logpri_t *lp)
{
    if (!strcasecmp (p, "emerg"))
        *lp = CMB_LOG_EMERG;
    else if (!strcasecmp (p, "alert"))
        *lp = CMB_LOG_ALERT;
    else if (!strcasecmp (p, "crit"))
        *lp = CMB_LOG_CRIT;
    else if (!strcasecmp (p, "err") || !strcasecmp (p, "error"))
        *lp = CMB_LOG_ERR;
    else if (!strcasecmp (p, "warning") || !strcasecmp (p, "warn"))
        *lp = CMB_LOG_WARNING;
    else if (!strcasecmp (p, "notice"))
        *lp = CMB_LOG_NOTICE;
    else if (!strcasecmp (p, "info"))
        *lp = CMB_LOG_INFO;
    else if (!strcasecmp (p, "debug"))
        *lp = CMB_LOG_DEBUG;
    else
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
