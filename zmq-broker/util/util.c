/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <json/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <math.h>
#include <limits.h>
#include <assert.h>
#include <zmq.h>

#include "util.h"
#include "log.h"

static struct timespec ts_diff (struct timespec start, struct timespec end)
{
        struct timespec temp;
        if ((end.tv_nsec-start.tv_nsec)<0) {
                temp.tv_sec = end.tv_sec-start.tv_sec-1;
                temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
        } else {
                temp.tv_sec = end.tv_sec-start.tv_sec;
                temp.tv_nsec = end.tv_nsec-start.tv_nsec;
        }
        return temp;
}

double monotime_since (struct timespec t0)
{
    struct timespec ts, d;
    clock_gettime (CLOCK_MONOTONIC, &ts);

    d = ts_diff (t0, ts);

    return ((double) d.tv_sec * 1000 + (double) d.tv_nsec / 1000000);
}

void monotime (struct timespec *tp)
{
    clock_gettime (CLOCK_MONOTONIC, tp);
}

bool monotime_isset (struct timespec t)
{
    return (t.tv_sec || t.tv_nsec);
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

int setenvf (const char *name, int overwrite, const char *fmt, ...)
{
    va_list ap;
    char *val;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return (rc);

    rc = setenv (name, val, overwrite);
    free (val);
    return (rc);
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

int util_json_size (json_object *o)
{
    const char *s = json_object_to_json_string (o);
    return strlen (s);
}

bool util_json_match (json_object *o1, json_object *o2)
{
    const char *s1 = json_object_to_json_string (o1);
    const char *s2 = json_object_to_json_string (o2);

    return !strcmp (s1, s2);
}

void util_json_encode (json_object *o, char **zbufp, unsigned int *zlenp)
{
    const char *s = json_object_to_json_string (o);
    unsigned int s_len = strlen (s);

    *zbufp = xstrdup (s);
    *zlenp = s_len;
}

void util_json_decode (json_object **op, char *zbuf, unsigned int zlen)
{
    json_object *o;
    struct json_tokener *tok;
    char *s;
    int s_len;

    s_len = zlen;
    s = zbuf; 

    if (!(tok = json_tokener_new ()))
        oom ();
    o = json_tokener_parse_ex (tok, s, s_len);
    json_tokener_free (tok); 
    *op = o;
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

void util_json_object_add_int64 (json_object *o, char *name, int64_t i)
{
    json_object *no;

    if (!(no = json_object_new_int64 (i)))
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

/* Z85 (1 char pad len + data + pad)
 */
void util_json_object_add_data (json_object *o, char *name,
                                uint8_t *dat, int len)
{
    int r = (len + 1) % 4;
    int padlen = r > 0 ? 4 - r : 0;
    int dlen = len + 1 + padlen;
    uint8_t *d = xzmalloc (dlen);
    char *p, *s = xzmalloc (dlen * 1.25 + 1);

    d[0] = padlen;
    memcpy (&d[1], dat, len);
    p = zmq_z85_encode (s, d, dlen);
    assert (p != NULL);

    util_json_object_add_string (o, name, s);
    free (s);
    free (d);
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

int util_json_object_get_int64 (json_object *o, char *name, int64_t *ip)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *ip = json_object_get_int64 (no);
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

/* Z85 (1 char pad len + data + pad)
 */
int util_json_object_get_data (json_object *o, char *name,
                               uint8_t **datp, int *lenp)
{
    const char *s;
    int dlen, len;
    uint8_t *d, *p;

    if (util_json_object_get_string (o, name, &s) == -1)
        return -1;
    dlen = strlen (s) * 0.8;
    d = xzmalloc (dlen);
    p = zmq_z85_decode (d, (char *)s);
    assert (p != NULL);
    len = dlen - 1 - d[0];
    memmove (&d[0], &d[1], len);
    *datp = d;
    *lenp = len;    
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

json_object *rusage_to_json (struct rusage *ru)
{
    json_object *o = util_json_object_new_object ();
    util_json_object_add_timeval (o, "utime", &ru->ru_utime);
    util_json_object_add_timeval (o, "stime", &ru->ru_stime);
    util_json_object_add_int64 (o, "maxrss", ru->ru_maxrss);
    util_json_object_add_int64 (o, "ixrss", ru->ru_ixrss);
    util_json_object_add_int64 (o, "idrss", ru->ru_idrss);
    util_json_object_add_int64 (o, "isrss", ru->ru_isrss);
    util_json_object_add_int64 (o, "minflt", ru->ru_minflt);
    util_json_object_add_int64 (o, "majflt", ru->ru_majflt);
    util_json_object_add_int64 (o, "nswap", ru->ru_nswap);
    util_json_object_add_int64 (o, "inblock", ru->ru_inblock);
    util_json_object_add_int64 (o, "oublock", ru->ru_oublock);
    util_json_object_add_int64 (o, "msgsnd", ru->ru_msgsnd);
    util_json_object_add_int64 (o, "msgrcv", ru->ru_msgrcv);
    util_json_object_add_int64 (o, "nsignals", ru->ru_nsignals);
    util_json_object_add_int64 (o, "nvcsw", ru->ru_nvcsw);
    util_json_object_add_int64 (o, "nivcsw", ru->ru_nivcsw);
    return o;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
