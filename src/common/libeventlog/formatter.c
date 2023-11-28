/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <string.h>
#include <time.h>
#include <errno.h>

#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"

#include "eventlog.h"
#include "formatter.h"

enum entry_format {
    EVENTLOG_ENTRY_TEXT,
    EVENTLOG_ENTRY_JSON
};

enum timestamp_format {
    EVENTLOG_TIMESTAMP_RAW,
    EVENTLOG_TIMESTAMP_ISO,
    EVENTLOG_TIMESTAMP_OFFSET
};

struct eventlog_formatter {
    /*  End of line separator for entries */
    const char *endl;

    /*  Emit unformatted output (RFC 18 JSON) */
    unsigned int unformatted:1;

    /*  Timestamp and entry formats for non RFC 18 operation: */
    enum timestamp_format ts_format;
    enum entry_format entry_format;

    /*  Initial timestamp */
    double t0;
};

void eventlog_formatter_destroy (struct eventlog_formatter *evf)
{
    if (evf) {
        int saved_errno = errno;
        free (evf);
        errno = saved_errno;
    }
}

struct eventlog_formatter *eventlog_formatter_create ()
{
    struct eventlog_formatter *evf;

    if (!(evf = calloc (1, sizeof (*evf))))
        return NULL;
    evf->endl = "\n";
    return evf;
}

void eventlog_formatter_set_no_newline (struct eventlog_formatter *evf)
{
    if (evf) {
        evf->endl = "";
    }
}

void eventlog_formatter_update_t0 (struct eventlog_formatter *evf, double ts)
{
    if (evf) {
        if (evf->t0 == 0.)
            evf->t0 = ts;
    }
}

int eventlog_formatter_set_timestamp_format (struct eventlog_formatter *evf,
                                             const char *format)
{
    if (!evf || !format) {
        errno = EINVAL;
        return -1;
    }
    if (strcasecmp (format, "raw") == 0)
        evf->ts_format = EVENTLOG_TIMESTAMP_RAW;
    else if (strcasecmp (format, "iso") == 0)
        evf->ts_format = EVENTLOG_TIMESTAMP_ISO;
    else if (strcasecmp (format, "offset") == 0)
        evf->ts_format = EVENTLOG_TIMESTAMP_OFFSET;
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int eventlog_formatter_set_format (struct eventlog_formatter *evf,
                                   const char *format)
{
    if (!evf || !format) {
        errno = EINVAL;
        return -1;
    }
    if (strcasecmp (format, "text") == 0)
        evf->unformatted = false;
    else if (strcasecmp (format, "json") == 0)
        evf->unformatted = true;
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void eventlog_formatter_reset (struct eventlog_formatter *evf)
{
    if (evf) {
        evf->t0 = 0.;
    }
}

static int event_timestamp (struct eventlog_formatter *evf,
                            flux_error_t *errp,
                            double timestamp,
                            char *buf,
                            size_t size)
{
    if (evf->ts_format == EVENTLOG_TIMESTAMP_RAW) {
        if (snprintf (buf, size, "%lf", timestamp) >= size)
            return errprintf (errp, "buffer truncated writing timestamp");
    }
    else if (evf->ts_format == EVENTLOG_TIMESTAMP_ISO) {
        time_t sec = timestamp;
        unsigned long usec = (timestamp - sec)*1E6;
        struct tm tm;
        if (!gmtime_r (&sec, &tm))
            return errprintf (errp,
                              "gmtime(%lf): %s",
                              timestamp,
                              strerror (errno));
        if (strftime (buf, size, "%Y-%m-%dT%T", &tm) == 0)
            return errprintf (errp,
                              "strftime(%lf): %s",
                              timestamp,
                              strerror (errno));
        size -= strlen (buf);
        buf += strlen (buf);
        if (snprintf (buf, size, ".%.6luZ", usec) >= size)
            return errprintf (errp,
                              "buffer truncated writing ISO 8601 timestamp");
    }
    else { /* EVENTLOG_TIMESTAMP_OFFSET */
        eventlog_formatter_update_t0 (evf, timestamp);
        timestamp -= evf->t0;
        if (snprintf (buf, size, "%lf", timestamp) >= size)
            return errprintf (errp,
                              "buffer truncated writing timestamp offset");
    }
    return 0;
}

static int entry_format_text (struct eventlog_formatter *evf,
                              flux_error_t *errp,
                              json_t *event,
                              double timestamp,
                              const char *name,
                              json_t *context,
                              FILE *fp)
{
    char ts[128];

    if (event_timestamp (evf, errp, timestamp, ts, sizeof (ts)) < 0)
        return -1;

    if (fprintf (fp, "%s %s", ts, name) < 0)
        return errprintf (errp, "fprintf: %s", strerror (errno));

    if (context) {
        const char *key;
        json_t *value;
        json_object_foreach (context, key, value) {
            char *sval;
            int rc;
            sval = json_dumps (value, JSON_ENCODE_ANY|JSON_COMPACT);
            rc = fprintf (fp, " %s=%s", key, sval);
            free (sval);
            if (rc < 0)
                return errprintf (errp, "fprintf: %s", strerror (errno));
        }
    }
    fputs (evf->endl, fp);
    return 0;
}

int eventlog_entry_dumpf (struct eventlog_formatter *evf,
                          FILE *fp,
                          flux_error_t *errp,
                          json_t *event)
{
    double timestamp;
    json_t *context;
    const char *name;

    if (!evf || !event || !fp) {
        errno = EINVAL;
        return -1;
    }

    /* Validity check:
     */
    if (eventlog_entry_parse (event, &timestamp, &name, &context) < 0)
        return errprintf (errp, "eventlog_entry_parse: %s", strerror (errno));

    if (evf->unformatted) {
        if (json_dumpf (event, fp, JSON_COMPACT) < 0)
            return errprintf (errp, "json_dumpf failed");
        fputs (evf->endl, fp);
        return 0;
    }

    return entry_format_text (evf, errp, event, timestamp, name, context, fp);
}

char *eventlog_entry_dumps (struct eventlog_formatter *evf,
                            flux_error_t *errp,
                            json_t *event)
{
    size_t size;
    char *result = NULL;
    FILE *fp;

    if (!evf || !event) {
        errno = EINVAL;
        return NULL;
    }
    if (!(fp = open_memstream (&result, &size))) {
        errprintf (errp, "open_memstream: %s", strerror (errno));
        return NULL;
    }
    if (eventlog_entry_dumpf (evf, fp, errp, event) < 0) {
        fclose (fp);
        free (result);
        return NULL;
    }
    fclose (fp);
    return result;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
