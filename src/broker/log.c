/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* CAUTION: logging errors with `flux_log()` here could result in
 * deadlock.  Errors that need to be seen should be logged to stderr
 * instead.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"
#include "src/common/libutil/timestamp.h"
#include "ccan/str/str.h"

#include "log.h"

typedef enum { MODE_LEADER, MODE_LOCAL } stderr_mode_t;
typedef enum { LOG_NO_TIMESTAMP=1 } log_flags_t;

/* See descriptions in flux-broker-attributes(7) */
static const int default_ring_size = 1024;
static const int default_forward_level = LOG_DEBUG;
static const int default_critical_level = LOG_CRIT;
static const int default_stderr_level = LOG_ERR;
static const stderr_mode_t default_stderr_mode = MODE_LEADER;
static const int default_level = LOG_DEBUG;

typedef struct {
    flux_t *h;
    flux_msg_handler_t **handlers;
    uint32_t rank;
    char *filename;
    FILE *f;
    int forward_level;
    int critical_level;
    int stderr_level;
    stderr_mode_t stderr_mode;
    int level;
    zlist_t *buf;
    int ring_size;
    int seq;
    struct flux_msglist *followers;
} logbuf_t;

struct logbuf_entry {
    char *buf;
    int seq;
};

void logbuf_destroy (logbuf_t *logbuf);

static void logbuf_entry_destroy (struct logbuf_entry *e)
{
    if (e) {
        if (e->buf)
            free (e->buf);
        free (e);
    }
}

/* Create a logbuf entry from RFC 5424 formatted buf.
 * Since buf may not have a terminating \0, add one so that it can be
 * treated as a string when returned in a log.dmesg response.
 */
static struct logbuf_entry *logbuf_entry_create (const char *buf, int len)
{
    struct logbuf_entry *e = calloc (1, sizeof (*e));
    if (!e)
        return NULL;
    if (!(e->buf = malloc (len + 1))) {
        free (e);
        return NULL;
    }
    memcpy (e->buf, buf, len);
    e->buf[len] = '\0';
    return e;
}

static void logbuf_trim (logbuf_t *logbuf, int size)
{
    struct logbuf_entry *e;
    while (zlist_size (logbuf->buf) > size) {
        e = zlist_pop (logbuf->buf);
        logbuf_entry_destroy (e);
    }
}

static int append_new_entry (logbuf_t *logbuf, const char *buf, int len)
{
    struct logbuf_entry *e;
    const flux_msg_t *msg;

    if (logbuf->ring_size > 0) {
        logbuf_trim (logbuf, logbuf->ring_size - 1);
        if (!(e = logbuf_entry_create (buf, len))) {
            errno = ENOMEM;
            return -1;
        }
        e->seq = logbuf->seq++;
        if (zlist_append (logbuf->buf, e) < 0) {
            logbuf_entry_destroy (e);
            errno = ENOMEM;
            return -1;
        }
        msg = flux_msglist_first (logbuf->followers);
        while (msg) {
            if (flux_respond (logbuf->h, msg, e->buf) < 0)
                log_err ("error responding to log.dmesg request");
            msg = flux_msglist_next (logbuf->followers);
        }
    }
    return 0;
}

static logbuf_t *logbuf_create (void)
{
    logbuf_t *logbuf = calloc (1, sizeof (*logbuf));
    if (!logbuf) {
        errno = ENOMEM;
        goto cleanup;
    }
    logbuf->forward_level = default_forward_level;
    logbuf->critical_level = default_critical_level;
    logbuf->stderr_level = default_stderr_level;
    logbuf->stderr_mode = default_stderr_mode;
    logbuf->level = default_level;
    logbuf->ring_size = default_ring_size;
    if (!(logbuf->buf = zlist_new ())) {
        errno = ENOMEM;
        goto cleanup;
    }
    if (!(logbuf->followers = flux_msglist_create ()))
        goto cleanup;
    return logbuf;
cleanup:
    logbuf_destroy (logbuf);
    return NULL;
}

void logbuf_destroy (logbuf_t *logbuf)
{
    if (logbuf) {
        if (logbuf->buf) {
            logbuf_trim (logbuf, 0);
            zlist_destroy (&logbuf->buf);
        }
        /* logbuf_destroy() would be called after local connector
         * unloaded, so no need to send ENODATA to followers */
        flux_msglist_destroy (logbuf->followers);
        if (logbuf->f)
            (void)fclose (logbuf->f);
        if (logbuf->filename)
            free (logbuf->filename);
        free (logbuf);
    }
}

static int logbuf_set_forward_level (logbuf_t *logbuf, int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        errno = EINVAL;
        return -1;
    }
    logbuf->forward_level = level;
    return 0;
}

static int logbuf_set_critical_level (logbuf_t *logbuf, int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        errno = EINVAL;
        return -1;
    }
    logbuf->critical_level = level;
    return 0;
}

static int logbuf_set_stderr_level (logbuf_t *logbuf, int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        errno = EINVAL;
        return -1;
    }
    logbuf->stderr_level = level;
    return 0;
}

static int logbuf_set_level (logbuf_t *logbuf, int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        errno = EINVAL;
        return -1;
    }
    logbuf->level = level;
    return 0;
}


static int logbuf_set_ring_size (logbuf_t *logbuf, int size)
{
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }
    logbuf_trim (logbuf, size);
    logbuf->ring_size = size;
    return 0;
}

/* Set the log filename (rank 0 only).
 * Allow other ranks to try to set this without effect
 * so that the same broker options can be used across a session.
 */
static int logbuf_set_filename (logbuf_t *logbuf, const char *destination)
{
    char *filename;
    FILE *f;
    if (logbuf->rank > 0)
        return 0;
    if (!(f = fopen (destination, "a")))
        return -1;
    if (!(filename = strdup (destination))) {
        fclose (f);
        return -1;
    }
    if (logbuf->filename)
        free (logbuf->filename);
    if (logbuf->f)
        fclose (logbuf->f);
    logbuf->f = f;
    logbuf->filename = filename;
    return 0;
}

static const char *int_to_string (int n)
{
    static char s[32]; // ample room to avoid overflow
    (void)snprintf (s, sizeof (s), "%d", n);
    return s;
}

static int attr_get_log (const char *name, const char **val, void *arg)
{
    logbuf_t *logbuf = arg;

    if (streq (name, "log-forward-level"))
        *val = int_to_string (logbuf->forward_level);
    else if (streq (name, "log-critical-level"))
        *val = int_to_string (logbuf->critical_level);
    else if (streq (name, "log-stderr-level"))
        *val = int_to_string (logbuf->stderr_level);
    else if (streq (name, "log-stderr-mode"))
        *val = logbuf->stderr_mode == MODE_LEADER ? "leader" : "local";
    else if (streq (name, "log-ring-size"))
        *val = int_to_string (logbuf->ring_size);
    else if (streq (name, "log-filename"))
        *val = logbuf->filename;
    else if (streq (name, "log-level"))
        *val = int_to_string (logbuf->level);
    else {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static int attr_set_log (const char *name, const char *val, void *arg)
{
    logbuf_t *logbuf = arg;
    int rc = -1;

    if (streq (name, "log-forward-level")) {
        int level = strtol (val, NULL, 10);
        if (logbuf_set_forward_level (logbuf, level) < 0)
            goto done;
    } else if (streq (name, "log-critical-level")) {
        int level = strtol (val, NULL, 10);
        if (logbuf_set_critical_level (logbuf, level) < 0)
            goto done;
    } else if (streq (name, "log-stderr-level")) {
        int level = strtol (val, NULL, 10);
        if (logbuf_set_stderr_level (logbuf, level) < 0)
            goto done;
    } else if (streq (name, "log-stderr-mode")) {
        if (streq (val, "leader"))
            logbuf->stderr_mode = MODE_LEADER;
        else if (streq (val, "local"))
            logbuf->stderr_mode = MODE_LOCAL;
        else {
            errno = EINVAL;
            goto done;
        }
    } else if (streq (name, "log-ring-size")) {
        int size = strtol (val, NULL, 10);
        if (logbuf_set_ring_size (logbuf, size) < 0)
            goto done;
    } else if (streq (name, "log-filename")) {
        if (logbuf_set_filename (logbuf, val) < 0)
            goto done;
    } else if (streq (name, "log-level")) {
        int level = strtol (val, NULL, 10);
        if (logbuf_set_level (logbuf, level) < 0)
            goto done;
    } else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static int logbuf_register_attrs (logbuf_t *logbuf, attr_t *attrs)
{
    int rc = -1;

    /* log-filename
     * Only allowed to be set on rank 0 (ignore initial value on rank > 0).
     */
    if (logbuf->rank == 0) {
        if (attr_add_active (attrs, "log-filename", 0,
                             attr_get_log, attr_set_log, logbuf) < 0)
            goto done;
    } else {
        (void)attr_delete (attrs, "log-filename", true);
        if (attr_add (attrs, "log-filename", NULL, ATTR_IMMUTABLE) < 0)
            goto done;
    }

    if (attr_add_active (attrs, "log-stderr-level", 0,
                         attr_get_log, attr_set_log, logbuf) < 0)
        goto done;
    if (attr_add_active (attrs, "log-stderr-mode", 0,
                         attr_get_log, attr_set_log, logbuf) < 0)
        goto done;
    if (attr_add_active (attrs, "log-level", 0,
                         attr_get_log, attr_set_log, logbuf) < 0)
        goto done;
    if (attr_add_active (attrs, "log-forward-level", 0,
                         attr_get_log, attr_set_log, logbuf) < 0)
        goto done;
    if (attr_add_active (attrs, "log-critical-level", 0,
                         attr_get_log, attr_set_log, logbuf) < 0)
        goto done;
    if (attr_add_active (attrs, "log-ring-size", 0,
                         attr_get_log, attr_set_log, logbuf) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int logbuf_forward (logbuf_t *logbuf, const char *buf, int len)
{
    flux_future_t *f;
    if (!(f = flux_rpc_raw (logbuf->h, "log.append", buf, len,
                              FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

static void log_timestamp (FILE *fp,
                           struct stdlog_header *hdr,
                           int flags)
{
    struct tm tm;
    struct timeval tv;
    char datetime[16]; /* 'MMM DD HH:MM:SS' */
    char timezone[16]; /* TZ abbrev should be short, give 15 chars max */

    if (flags & LOG_NO_TIMESTAMP)
        return;
    if (timestamp_parse (hdr->timestamp, &tm, &tv) < 0
        || strftime (datetime, sizeof (datetime), "%b %d %T", &tm) == 0
        || strftime (timezone, sizeof (timezone), "%Z", &tm) == 0)
        fprintf (fp, "%s ", hdr->timestamp);
    fprintf (fp, "%s.%06ld %s ", datetime, tv.tv_usec, timezone);
}

/* Log a message to 'fp', if non-NULL.
 * Set flags to LOG_NO_TIMESTAMP to suppress timestamp.
 */
static void log_fp (FILE *fp, int flags, const char *buf, int len)
{
    struct stdlog_header hdr;
    const char *msg;
    int msglen, severity;
    uint32_t nodeid;

    if (fp) {
        if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
            fprintf (fp, "%.*s\n", len, buf);
        else {
            nodeid = strtoul (hdr.hostname, NULL, 10);
            severity = STDLOG_SEVERITY (hdr.pri);
            log_timestamp (fp, &hdr, flags);
            fprintf (fp, "%s.%s[%" PRIu32 "]: %.*s\n",
                     hdr.appname,
                     stdlog_severity_to_string (severity),
                     nodeid,
                     msglen, msg);
        }
    }
    fflush (fp);
}

static int logbuf_append (logbuf_t *logbuf, const char *buf, int len)
{
    bool logged_stderr = false;
    int rc = 0;
    uint32_t rank = FLUX_NODEID_ANY;
    int severity = LOG_INFO;
    struct stdlog_header hdr;

    stdlog_init (&hdr);
    if (stdlog_decode (buf, len, &hdr, NULL, NULL, NULL, NULL) == 0) {
        rank = strtoul (hdr.hostname, NULL, 10);
        severity = STDLOG_SEVERITY (hdr.pri);
    }

    if (rank == logbuf->rank) {
        if (severity <= logbuf->level) {
            if (append_new_entry (logbuf, buf, len) < 0)
                rc = -1;
        }
        if (severity <= logbuf->critical_level
                    || (severity <= logbuf->stderr_level
                    && logbuf->stderr_mode == MODE_LOCAL)) {
            int flags = 0;
            if (logbuf->stderr_mode == MODE_LOCAL)
                flags |= LOG_NO_TIMESTAMP; // avoid dup in syslog journal
            log_fp (stderr, flags, buf, len);
            logged_stderr = true;
        }
    }
    if (severity <= logbuf->forward_level) {
        if (logbuf->rank == 0) {
            log_fp (logbuf->f, 0, buf, len);
        } else {
            if (logbuf_forward (logbuf, buf, len) < 0)
                rc = -1;
        }
    }
    if (!logged_stderr && severity <= logbuf->stderr_level
                       && logbuf->stderr_mode == MODE_LEADER
                       && logbuf->rank == 0) {
            log_fp (stderr, 0, buf, len);
    }
    return rc;
}

/* Receive a log entry.
 * This is a flux_log_f that is passed to flux_log_redirect()
 * to capture log entries from the broker itself.
 */
static void logbuf_append_redirect (const char *buf, int len, void *arg)
{
    logbuf_t *logbuf = arg;
    (void)logbuf_append (logbuf, buf, len);
}

/* N.B. log requests have no response.
 */
static void append_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    logbuf_t *logbuf = arg;
    uint32_t matchtag;
    const char *buf;
    int len;

    if (flux_msg_get_matchtag (msg, &matchtag) < 0) {
        log_msg ("%s: malformed log request", __FUNCTION__);
        return;
    }
    if (flux_request_decode_raw (msg, NULL, (const void **)&buf, &len) < 0)
        goto error;
    if (logbuf_append (logbuf, buf, len) < 0)
        goto error;
    if (matchtag != FLUX_MATCHTAG_NONE) {
        if (flux_respond (h, msg, NULL) < 0)
            log_err ("%s: error responding to log request", __FUNCTION__);
    }
    return;
error:
    if (matchtag != FLUX_MATCHTAG_NONE) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            log_err ("%s: error responding to log request", __FUNCTION__);
    }
}

static void clear_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    logbuf_t *logbuf = arg;

    logbuf_trim (logbuf, 0);
    flux_respond (h, msg, NULL);
    return;
}

static void dmesg_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    logbuf_t *logbuf = arg;
    struct logbuf_entry *e;
    int follow;
    int nobacklog = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:b s?b}",
                             "follow", &follow,
                             "nobacklog", &nobacklog) < 0)
        goto error;

    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        goto error;
    }

    if (!nobacklog) {
        e = zlist_first (logbuf->buf);
        while (e) {
            if (flux_respond (h, msg, e->buf) < 0) {
                log_err ("error responding to log.dmesg request");
                goto error;
            }
            e = zlist_next (logbuf->buf);
        }
    }

    if (follow) {
        if (flux_msglist_append (logbuf->followers, msg) < 0)
            goto error;
    }
    else {
        errno = ENODATA;
        goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        log_err ("error responding to log.dmesg request");
}

static void disconnect_request_cb (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    logbuf_t *logbuf = arg;

    flux_msglist_disconnect (logbuf->followers, msg);
}

static void cancel_request_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    logbuf_t *logbuf = arg;

    flux_msglist_cancel (h, logbuf->followers, msg);
}

static void stats_request_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    logbuf_t *logbuf = arg;

    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i}",
                           "ring-used", (int)zlist_size (logbuf->buf),
                           "count", logbuf->seq) < 0)
        flux_log_error (h, "error responding to log.stats-get");
}


static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "log.append",         append_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.clear",          clear_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.dmesg",          dmesg_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.disconnect",     disconnect_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.cancel",         cancel_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.stats-get",      stats_request_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static void logbuf_finalize (void *arg)
{
    logbuf_t *logbuf = arg;
    flux_msg_handler_delvec (logbuf->handlers);
    logbuf_destroy (logbuf);
    /* FIXME: need logbuf_unregister_attrs() */
}

int logbuf_initialize (flux_t *h, uint32_t rank, attr_t *attrs)
{
    logbuf_t *logbuf = logbuf_create ();
    if (!logbuf)
        goto error;
    logbuf->h = h;
    logbuf->rank = rank;
    if (logbuf_register_attrs (logbuf, attrs) < 0)
        goto error;
    if (flux_msg_handler_addvec (h, htab, logbuf, &logbuf->handlers) < 0)
        goto error;
    flux_log_set_appname (h, "broker");
    flux_log_set_redirect (h, logbuf_append_redirect, logbuf);
    flux_aux_set (h, "flux::logbuf", logbuf, logbuf_finalize);
    return 0;
error:
    logbuf_destroy (logbuf);
    /* FIXME: need logbuf_unregister_attrs() */
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
