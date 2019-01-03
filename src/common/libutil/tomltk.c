/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <jansson.h>

#include "src/common/libtomlc99/toml.h"
#include "timestamp.h"
#include "tomltk.h"

static int table_to_json (toml_table_t *tab, json_t **op);

static void errprintf (struct tomltk_error *error,
                       const char *filename, int lineno,
                       const char *fmt, ...)
{
    va_list ap;
    int saved_errno = errno;

    if (error) {
        memset (error, 0, sizeof (*error));
        va_start (ap, fmt);
        (void)vsnprintf (error->errbuf, sizeof (error->errbuf), fmt, ap);
        va_end (ap);
        if (filename)
            strncpy (error->filename, filename, PATH_MAX);
        error->lineno = lineno;
    }
    errno = saved_errno;
}

/* Given an error message response from toml_parse(), parse the
 * error message into line number and message, e.g.
 *   "line 42: bad key"
 * is parsed to:
 *   error->lineno=42, error->errbuf="bad key"
 */
static void errfromtoml (struct tomltk_error *error,
                         const char *filename, char *errstr)
{
    if (error) {
        char *msg = errstr;
        int lineno = -1;
        if (!strncmp (errstr, "line ", 5)) {
            lineno = strtoul (errstr + 5, &msg, 10);
            if (!strncmp (msg, ": ", 2))
                msg += 2;
        }
        return errprintf (error, filename, lineno, "%s", msg);
    }
}

/* Convert from TOML timestamp to struct tm (POSIX broken out time).
 */
static int tstotm (toml_timestamp_t *ts, struct tm *tm)
{
    if (!ts || !tm || !ts->year || !ts->month || !ts->day
                   || !ts->hour  || !ts->minute || !ts->second)
        return -1;
    memset (tm, 0, sizeof (*tm));
    tm->tm_year = *ts->year - 1900;
    tm->tm_mon = *ts->month - 1;
    tm->tm_mday = *ts->day;
    tm->tm_hour = *ts->hour;
    tm->tm_min = *ts->minute;
    tm->tm_sec = *ts->second;
    return 0;
}

int tomltk_ts_to_epoch (toml_timestamp_t *ts, time_t *tp)
{
    struct tm tm;
    time_t t;

    if (!ts || tstotm (ts, &tm) < 0 || (t = timegm (&tm)) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (tp)
        *tp = t;
    return 0;
}

int tomltk_json_to_epoch (const json_t *obj, time_t *tp)
{
    const char *s;

    /* N.B. 'O' specifier not used, hence obj is not in danger
     * of being modified by json_unpack.
     */
    if (!obj || json_unpack ((json_t *)obj, "{s:s}", "iso-8601-ts", &s) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (timestamp_fromstr (s, tp) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

json_t *tomltk_epoch_to_json (time_t t)
{
    char timebuf[80];
    json_t *obj;

    if (timestamp_tostr (t, timebuf, sizeof (timebuf)) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(obj = json_pack ("{s:s}", "iso-8601-ts", timebuf))) {
        errno = EINVAL;
        return NULL;
    }
    return obj;
}

/* Convert raw TOML value from toml_raw_in() or toml_raw_at() to JSON.
 */
static int value_to_json (const char *raw, json_t **op)
{
    char *s = NULL;
    int b;
    int64_t i;
    double d;
    toml_timestamp_t ts;
    json_t *obj;

    if (toml_rtos (raw, &s) == 0) {
        obj = json_string (s);
        free (s);
        if (!obj)
            goto nomem;
    }
    else if (toml_rtob (raw, &b) == 0) {
        if (!(obj = b ? json_true () : json_false ()))
            goto nomem;
    }
    else if (toml_rtoi (raw, &i) == 0) {
        if (!(obj = json_integer (i)))
            goto nomem;
    }
    else if (toml_rtod (raw, &d) == 0) {
        if (!(obj = json_real (d)))
            goto nomem;
    }
    else if (toml_rtots (raw, &ts) == 0) {
        time_t t;
        if (tomltk_ts_to_epoch (&ts, &t) < 0
                || !(obj = tomltk_epoch_to_json (t)))
            goto error;
    }
    else {
        errno = EINVAL;
        goto error;
    }
    *op = obj;
    return 0;
nomem:
    errno = ENOMEM;
error:
    return -1;
}

/* Convert TOML array to JSON.
 */
static int array_to_json (toml_array_t *arr, json_t **op)
{
    int i;
    int saved_errno;
    json_t *obj;

    if (!(obj = json_array ()))
        goto nomem;
    for (i = 0; ; i++) {
        const char *raw;
        json_t *val;
        toml_table_t *tab;
        toml_array_t *subarr;

        if ((raw = toml_raw_at (arr, i))) {
            if (value_to_json (raw, &val) < 0)
                goto error;
        }
        else if ((tab = toml_table_at (arr, i))) {
            if (table_to_json (tab, &val) < 0)
                goto error;
        }
        else if ((subarr = toml_array_at (arr, i))) {
            if (array_to_json (subarr, &val) < 0)
                goto error;
        }
        else
            break;
        if (json_array_append_new (obj, val) < 0) {
            json_decref (val);
            goto nomem;
        }
    }
    *op = obj;
    return 0;
nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (obj);
    errno = saved_errno;
    return -1;
}

/* Convert TOML table to JSON.
 */
static int table_to_json (toml_table_t *tab, json_t **op)
{
    int i;
    int saved_errno;
    json_t *obj;

    if (!(obj = json_object ()))
        goto nomem;
    for (i = 0; ; i++) {
        const char *key;
        const char *raw;
        toml_table_t *subtab;
        toml_array_t *arr;
        json_t *val = NULL;

        if (!(key = toml_key_in (tab, i)))
            break;
        if ((raw = toml_raw_in (tab, key))) {
            if (value_to_json (raw, &val) < 0)
                goto error;
        }
        else if ((subtab = toml_table_in (tab, key))) {
            if (table_to_json (subtab, &val) < 0)
                goto error;
        }
        else if ((arr = toml_array_in (tab, key))) {
            if (array_to_json (arr, &val) < 0)
                goto error;
        }
        if (json_object_set_new (obj, key, val) < 0) {
            json_decref (val);
            goto nomem;
        }
    }
    *op = obj;
    return 0;
nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (obj);
    errno = saved_errno;
    return -1;
}

json_t *tomltk_table_to_json (toml_table_t *tab)
{
    json_t *obj;

    if (!tab) {
        errno = EINVAL;
        return NULL;
    }
    if (table_to_json (tab, &obj) < 0)
        return NULL;
    return obj;
}

toml_table_t *tomltk_parse (const char *conf, int len,
                            struct tomltk_error *error)
{
    char errbuf[200];
    char *cpy;
    toml_table_t *tab;

    if (len < 0 || (!conf && len != 0)) {
        errprintf (error, NULL, -1, "invalid argument");
        errno = EINVAL;
        return NULL;
    }
    if (!(cpy = calloc (1, len + 1))) {
        errprintf (error, NULL, -1, "out of memory");
        errno = ENOMEM;
        return NULL;
    }
    memcpy (cpy, conf, len);
    tab = toml_parse (cpy, errbuf, sizeof (errbuf));
    free (cpy);
    if (!tab) {
        errfromtoml (error, NULL, errbuf);
        errno = EINVAL;
        return NULL;
    }
    return tab;
}

toml_table_t *tomltk_parse_file (const char *filename,
                                 struct tomltk_error *error)
{
    char errbuf[200];
    FILE *fp;
    toml_table_t *tab;

    if (!filename) {
        errprintf (error, NULL, -1, "invalid argument");
        errno = EINVAL;
        return NULL;
    }
    if (!(fp = fopen (filename, "r"))) {
        errprintf (error, filename, -1, "%s", strerror (errno));
        return NULL;
    }
    // N.B. toml_parse_file() doesn't give us any way to distinguish parse
    // error from read error
    tab = toml_parse_file (fp, errbuf, sizeof (errbuf));
    (void)fclose (fp);
    if (!tab) {
        errfromtoml (error, filename, errbuf);
        errno = EINVAL;
        return NULL;
    }
    return tab;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
