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

#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <flux/core.h>
#include <jansson.h>

#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/timestamp.h"
#include "src/common/libutil/ansi_color.h"

#include "eventlog.h"
#include "formatter.h"

enum {
    EVENTLOG_COLOR_NAME,
    EVENTLOG_COLOR_TIME,
    EVENTLOG_COLOR_TIMEBREAK,
    EVENTLOG_COLOR_KEY,
    EVENTLOG_COLOR_VALUE,
    EVENTLOG_COLOR_VALUE_NUM,
    EVENTLOG_COLOR_EXCEPTION,
};

static const char *eventlog_colors[] = {
    [EVENTLOG_COLOR_NAME]      = ANSI_COLOR_YELLOW,
    [EVENTLOG_COLOR_TIME]      = ANSI_COLOR_GREEN,
    [EVENTLOG_COLOR_TIMEBREAK] = ANSI_COLOR_BOLD ANSI_COLOR_GREEN,
    [EVENTLOG_COLOR_KEY]       = ANSI_COLOR_BLUE,
    [EVENTLOG_COLOR_VALUE]     = ANSI_COLOR_MAGENTA,
    [EVENTLOG_COLOR_VALUE_NUM] = ANSI_COLOR_GRAY,
    [EVENTLOG_COLOR_EXCEPTION] = ANSI_COLOR_BOLD ANSI_COLOR_RED,
};

enum entry_format {
    EVENTLOG_ENTRY_TEXT,
    EVENTLOG_ENTRY_JSON
};

enum timestamp_format {
    EVENTLOG_TIMESTAMP_RAW,
    EVENTLOG_TIMESTAMP_ISO,
    EVENTLOG_TIMESTAMP_OFFSET,
    EVENTLOG_TIMESTAMP_HUMAN,
};


struct eventlog_formatter {
    /*  End of line separator for entries */
    const char *endl;

    /*  Emit unformatted output (RFC 18 JSON) */
    unsigned int unformatted:1;

    /*  Enable color: */
    unsigned int color:1;

    /*  Also color event context key/values: */
    unsigned int context_color:1;

    /*  Timestamp and entry formats for non RFC 18 operation: */
    enum timestamp_format ts_format;
    enum entry_format entry_format;

    /*  Initial timestamp */
    double t0;

    /*  For relative timestamp output */
    struct tm last_tm;
    double last_ts;
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

int eventlog_formatter_set_color (struct eventlog_formatter *evf, int color)
{
    if (!evf || color < 0 || color > 1) {
        errno = EINVAL;
        return -1;
    }
    /* For now, always enable context colorization if evf->color is set:
     * (This is a separate variable to allow for future possible disablement)
     */
    evf->color = color;
    evf->context_color = color;
    return 0;
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
    else if (strcasecmp (format, "human") == 0
             || strcasecmp (format, "reltime") == 0)
        evf->ts_format = EVENTLOG_TIMESTAMP_HUMAN;
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
        evf->last_ts = 0.;
        memset (&evf->last_tm, 0, sizeof (struct tm));
    }
}

static const char *eventlog_color (struct eventlog_formatter *evf, int type)
{
    if (evf->color)
        return eventlog_colors [type];
    return "";
}

/*  Color the name of specific events
 */
static const char *eventlog_color_event_name (struct eventlog_formatter *evf,
                                              const char *name)
{
    if (evf->color) {
        if (streq (name, "exception"))
            return eventlog_colors [EVENTLOG_COLOR_EXCEPTION];
        else
            return eventlog_colors [EVENTLOG_COLOR_NAME];
    }
    return "";
}

static const char *eventlog_context_color (struct eventlog_formatter *evf,
                                           int type)
{
    if (evf->context_color)
        return eventlog_color (evf, type);
    return "";
}

static const char *eventlog_color_reset (struct eventlog_formatter *evf)
{
    if (evf->color)
        return ANSI_COLOR_RESET;
    return "";
}

static const char *
eventlog_context_color_reset (struct eventlog_formatter *evf)
{
    if (evf->context_color)
        return eventlog_color_reset (evf);
    return "";
}

static const char *months[] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec",
    NULL
};

static int event_timestamp_human (struct eventlog_formatter *evf,
                                  double timestamp,
                                  char *buf,
                                  size_t size)
{
    struct tm tm;

    if (timestamp_from_double (timestamp, &tm, NULL) < 0)
        return snprintf (buf,
                         size,
                         "%s[%+11.6f]%s",
                         eventlog_color (evf, EVENTLOG_COLOR_TIME),
                         timestamp,
                         eventlog_color_reset (evf));

    if (tm.tm_year == evf->last_tm.tm_year
        && tm.tm_mon == evf->last_tm.tm_mon
        && tm.tm_mday == evf->last_tm.tm_mday
        && tm.tm_hour == evf->last_tm.tm_hour
        && tm.tm_min == evf->last_tm.tm_min) {
        /*  Within same minute, print offset in sec */
        return snprintf (buf,
                         size,
                         "%s[%+11.6f]%s",
                         eventlog_color (evf, EVENTLOG_COLOR_TIME),
                         timestamp - evf->last_ts,
                         eventlog_color_reset (evf));
    }
    /* New minute. Save last timestamp,  print datetime */
    evf->last_ts = timestamp;
    evf->last_tm = tm;

    return snprintf (buf,
                     size,
                     "%s[%s%02d %02d:%02d]%s",
                     eventlog_color (evf, EVENTLOG_COLOR_TIMEBREAK),
                     months [tm.tm_mon],
                     tm.tm_mday,
                     tm.tm_hour,
                     tm.tm_min,
                     eventlog_color_reset (evf));
}


static int event_timestamp (struct eventlog_formatter *evf,
                            flux_error_t *errp,
                            double timestamp,
                            char *buf,
                            size_t size)
{
    if (evf->ts_format == EVENTLOG_TIMESTAMP_RAW) {
        if (snprintf (buf,
                      size,
                      "%s%lf%s",
                      eventlog_color (evf, EVENTLOG_COLOR_TIME),
                      timestamp,
                      eventlog_color_reset (evf)) >= size)
            return errprintf (errp, "buffer truncated writing timestamp");
    }
    else if (evf->ts_format == EVENTLOG_TIMESTAMP_HUMAN) {
        if (event_timestamp_human (evf, timestamp, buf, size) >= size)
            return errprintf (errp,
                              "buffer truncated writing relative timestamp");
    }
    else if (evf->ts_format == EVENTLOG_TIMESTAMP_ISO) {
        time_t sec = timestamp;
        unsigned long usec = (timestamp - sec)*1E6;
        struct tm tm;
        char tz[16];

        if (!localtime_r (&sec, &tm))
            return errprintf (errp,
                              "localtime(%lf): %s",
                              timestamp,
                              strerror (errno));
        if (snprintf (buf,
                      size,
                      "%s",
                      eventlog_color (evf, EVENTLOG_COLOR_TIME)) >= size)
            return errprintf (errp,
                              "failed to write timestamp color to buffer");
        size -= strlen (buf);
        buf += strlen (buf);
        if (strftime (buf, size, "%Y-%m-%dT%T", &tm) == 0
            || timestamp_tzoffset (&tm, tz, sizeof (tz)) < 0)
            return errprintf (errp,
                              "strftime(%lf): %s",
                              timestamp,
                              strerror (errno));
        size -= strlen (buf);
        buf += strlen (buf);
        if (snprintf (buf,
                      size,
                      ".%.6lu%s%s",
                      usec,
                      tz,
                      eventlog_color_reset (evf)) >= size)
            return errprintf (errp,
                              "buffer truncated writing ISO 8601 timestamp");
    }
    else { /* EVENTLOG_TIMESTAMP_OFFSET */
        eventlog_formatter_update_t0 (evf, timestamp);
        timestamp -= evf->t0;
        if (snprintf (buf,
                      size,
                      "%s%lf%s",
                      eventlog_color (evf, EVENTLOG_COLOR_TIME),
                      timestamp,
                      eventlog_color_reset (evf)) >= size)
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

    if (fprintf (fp,
                 "%s %s%s%s",
                 ts,
                 eventlog_color_event_name (evf, name),
                 name,
                 eventlog_color_reset (evf)) < 0)
        return errprintf (errp, "fprintf: %s", strerror (errno));

    if (context) {
        const char *key;
        json_t *value;
        json_object_foreach (context, key, value) {
            char *sval;
            int rc;
            int color = EVENTLOG_COLOR_VALUE;
            if (json_is_number (value))
                color = EVENTLOG_COLOR_VALUE_NUM;
            sval = json_dumps (value, JSON_ENCODE_ANY|JSON_COMPACT);
            rc = fprintf (fp,
                          " %s%s%s=%s%s%s",
                          eventlog_context_color (evf, EVENTLOG_COLOR_KEY),
                          key,
                          eventlog_context_color_reset (evf),
                          eventlog_context_color (evf, color),
                          sval,
                          eventlog_context_color_reset (evf));
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
