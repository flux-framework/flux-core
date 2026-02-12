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
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <pwd.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"
#include "src/common/libutil/timestamp.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "ccan/list/list.h"

#include "log.h"

typedef enum { MODE_LEADER, MODE_LOCAL } stderr_mode_t;
typedef enum { LOG_FOR_SYSTEMD=1 } log_flags_t;

/* See descriptions in flux-broker-attributes(7) */
static const int default_ring_size = 1024;
static const int default_forward_level = LOG_ERR;
static const int default_critical_level = LOG_CRIT;
static const int default_stderr_level = LOG_ERR;
static const int default_syslog_level = LOG_ERR;
static const bool default_syslog_enable = false;
static const stderr_mode_t default_stderr_mode = MODE_LEADER;
static const int default_level = LOG_DEBUG;

typedef struct {
    flux_t *h;
    attr_t *attrs;
    flux_msg_handler_t **handlers;
    uint32_t rank;
    char *filename;
    FILE *f;
    bool syslog_enable;
    int syslog_level;
    char *jobid_path;
    char *username;
    int forward_level;
    int critical_level;
    int stderr_level;
    stderr_mode_t stderr_mode;
    int level;
    struct list_head ring;
    size_t ring_count;
    size_t ring_size;
    int seq;
    struct flux_msglist *followers;
    int recv_local_count;
    int recv_remote_count;
} logbuf_t;

struct logbuf_entry {
    char *buf;
    int seq;
    struct list_node list;
};

void logbuf_destroy (logbuf_t *logbuf);

static void logbuf_entry_destroy (struct logbuf_entry *e)
{
    free (e);
}

/* Create a logbuf entry from RFC 5424 formatted buf.
 * Since buf may not have a terminating \0, add one so that it can be
 * treated as a string when returned in a log.dmesg response.
 */
static struct logbuf_entry *logbuf_entry_create (const char *buf, int len)
{
    struct logbuf_entry *e;

    if (!(e = calloc (1, sizeof (*e) + len + 1)))
        return NULL;
    e->buf = (char *)(e + 1);
    memcpy (e->buf, buf, len);
    return e;
}

static void logbuf_trim (logbuf_t *logbuf, int size)
{
    struct logbuf_entry *e;
    while (logbuf->ring_count > size) {
        e = list_pop (&logbuf->ring, struct logbuf_entry, list);
        logbuf_entry_destroy (e);
        logbuf->ring_count--;
    }
}

static int append_new_entry (logbuf_t *logbuf, const char *buf, int len)
{
    struct logbuf_entry *e;
    const flux_msg_t *msg;

    if (logbuf->ring_size > 0) {
        if (!(e = logbuf_entry_create (buf, len))) {
            errno = ENOMEM;
            return -1;
        }
        e->seq = logbuf->seq++;
        list_add_tail (&logbuf->ring, &e->list);
        logbuf->ring_count++;
        logbuf_trim (logbuf, logbuf->ring_size);

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
    list_head_init (&logbuf->ring);
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
        logbuf_trim (logbuf, 0);
        /* logbuf_destroy() would be called after local connector
         * unloaded, so no need to send ENODATA to followers */
        flux_msglist_destroy (logbuf->followers);
        if (logbuf->f)
            (void)fclose (logbuf->f);
        if (logbuf->syslog_enable)
            closelog ();
        free (logbuf->filename);
        free (logbuf->jobid_path);
        free (logbuf->username);
        free (logbuf);
    }
}

static int getattr_int (attr_t *attrs, const char *name, int *vp)
{
    const char *val;
    char *endptr;
    long long v;

    if (attr_get (attrs, name, &val) < 0)
        return -1;
    if (!val) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    v = strtoll (val, &endptr, 10);
    if (errno != 0
        || *endptr != '\0'
        || endptr == val
        || v < INT_MIN
        || v > INT_MAX) {
        errno = EINVAL;
        return -1;
    }
    *vp = v;
    return 0;
}

static int getattr_level (attr_t *attrs, const char *name, int *vp)
{
    int v;

    if (getattr_int (attrs, name, &v) < 0)
        return -1;
    if (v > LOG_DEBUG) { /* N.B. negative is ok (match nothing) */
        errno = EINVAL;
        return -1;
    }
    *vp = v;
    return 0;
}

static int getattr_mode (attr_t *attrs, const char *name, stderr_mode_t *vp)
{
    const char *val;
    stderr_mode_t mode;

    if (attr_get (attrs, name, &val) < 0)
        return -1;
    if (!val) {
        errno = EINVAL;
        return -1;
    }
    if (streq (val, "local"))
        mode = MODE_LOCAL;
    else if (streq (val, "leader"))
        mode = MODE_LEADER;
    else {
        errno = EINVAL;
        return -1;
    }
    *vp = mode;
    return 0;
}

static int getattr_size (attr_t *attrs, const char *name, size_t *vp)
{
    const char *val;
    char *endptr;
    long long v;

    if (attr_get (attrs, name, &val) < 0)
        return -1;
    if (!val) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    v = strtoll (val, &endptr, 10);
    if (errno != 0
        || *endptr != '\0'
        || endptr == val
        || v < 0
        || v > SIZE_MAX) {
        errno = EINVAL;
        return -1;
    }
    *vp = v;
    return 0;
}

static int setattr_int (attr_t *attrs, const char *name, long long v)
{
    char val[64];

    snprintf (val, sizeof (val), "%lld", v);
    return attr_set (attrs, name, val);
}

static int setattr_mode (attr_t *attrs, const char *name, stderr_mode_t mode)
{
    const char *val;
    switch (mode) {
        case MODE_LEADER:
            val = "leader";
            break;
        case MODE_LOCAL:
            val = "local";
            break;
    }
    return attr_set (attrs, name, val);
}

static int register_attr_level (attr_t *attrs,
                                const char *name,
                                int *value,
                                int default_value,
                                flux_error_t *errp)
{
    int level;

    if (getattr_level (attrs, name, &level) < 0) {
        if (errno != ENOENT)
            return errprintf (errp, "%s: %s", name, strerror (errno));
        if (setattr_int (attrs, name, default_value) < 0)
            return errprintf (errp, "%s: %s", name, strerror (errno));
        level = default_value;
    }
    *value = level;
    return 0;
}

static int register_attr_size (attr_t *attrs,
                               const char *name,
                               size_t *value,
                               size_t default_value,
                               flux_error_t *errp)
{
    size_t size;

    if (getattr_size (attrs, name, &size) < 0) {
        if (errno != ENOENT)
            return errprintf (errp, "%s: %s", name, strerror (errno));
        if (setattr_int (attrs, name, default_value) < 0)
            return errprintf (errp, "%s: %s", name, strerror (errno));
        size = default_value;
    }
    *value = size;
    return 0;
}

static int register_attr_bool (attr_t *attrs,
                               const char *name,
                               bool *value,
                               bool default_value,
                               flux_error_t *errp)
{
    int val;
    bool flag;

    if (getattr_int (attrs, name, &val) < 0) {
        if (errno != ENOENT)
            return errprintf (errp, "getattr %s: %s", name, strerror (errno));
        if (setattr_int (attrs, name, default_value ? 1 : 0) < 0)
            return errprintf (errp, "setattr %s: %s", name, strerror (errno));
        flag = default_value;
    }
    else {
        if (val == 0)
            flag = false;
        else if (val == 1)
            flag = true;
        else {
            errno = EINVAL;
            return errprintf (errp, "%s: value must be 0 or 1", name);
        }
    }
    *value = flag;
    return 0;
}

static int register_attr_mode (attr_t *attrs,
                               const char *name,
                               stderr_mode_t *value,
                               stderr_mode_t default_value,
                               flux_error_t *errp)
{
    stderr_mode_t mode;

    if (getattr_mode (attrs, name, &mode) < 0) {
        if (errno != ENOENT)
            return errprintf (errp, "getattr %s: %s", name, strerror (errno));
        if (setattr_mode (attrs, name, default_value) < 0)
            return errprintf (errp, "setattr %s: %s", name, strerror (errno));
        mode = default_value;
    }
    *value = mode;
    return 0;
}

static int logbuf_register_attrs (logbuf_t *logbuf, flux_error_t *errp)
{
    /* log-filename
     * Only allowed to be set on rank 0 (ignore initial value on rank > 0).
     */
    if (logbuf->rank == 0) {
        const char *path;
        if (attr_get (logbuf->attrs, "log-filename", &path) == 0) {
            if (path && !(logbuf->filename = strdup (path)))
                return errprintf (errp, "Error duplicating logfile name");
        }
    }
    else {
        (void)attr_delete (logbuf->attrs, "log-filename");
        if (attr_set (logbuf->attrs, "log-filename", NULL) < 0) {
            return errprintf (errp,
                              "setattr log-filename: %s",
                              strerror (errno));
        }
    }
    if (register_attr_level (logbuf->attrs,
                             "log-level",
                             &logbuf->level,
                             default_level,
                             errp) < 0)
        return -1;
    if (register_attr_level (logbuf->attrs,
                             "log-stderr-level",
                             &logbuf->stderr_level,
                             default_stderr_level,
                             errp) < 0)
        return -1;
    if (register_attr_level (logbuf->attrs,
                             "log-forward-level",
                             &logbuf->forward_level,
                             default_forward_level,
                             errp) < 0)
        return -1;
    if (register_attr_level (logbuf->attrs,
                             "log-critical-level",
                             &logbuf->critical_level,
                             default_critical_level,
                             errp) < 0)
        return -1;
    if (register_attr_level (logbuf->attrs,
                             "log-syslog-level",
                             &logbuf->syslog_level,
                             default_syslog_level,
                             errp) < 0)
        return -1;
    if (register_attr_size (logbuf->attrs,
                            "log-ring-size",
                            &logbuf->ring_size,
                            default_ring_size,
                            errp) < 0)
        return -1;
    if (register_attr_bool (logbuf->attrs,
                            "log-syslog-enable",
                            &logbuf->syslog_enable,
                            default_syslog_enable,
                            errp) < 0)
        return -1;
    if (register_attr_mode (logbuf->attrs,
                            "log-stderr-mode",
                             &logbuf->stderr_mode,
                             default_stderr_mode,
                             errp) < 0)
        return -1;
    return 0;
}

static int logbuf_forward (logbuf_t *logbuf, const char *buf, int len)
{
    flux_future_t *f;
    if (!(f = flux_rpc_raw (logbuf->h,
                            "log.append",
                            buf,
                            len,
                            FLUX_NODEID_UPSTREAM,
                            FLUX_RPC_NORESPONSE)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

static void log_timestamp (FILE *fp, struct stdlog_header *hdr)
{
    struct tm tm;
    struct timeval tv;
    char datetime[16]; /* 'MMM DD HH:MM:SS' */
    char timezone[16]; /* TZ abbrev should be short, give 15 chars max */
    char year[5];      /* 4 digit year */

    if (timestamp_parse (hdr->timestamp, &tm, &tv) < 0
        || strftime (datetime, sizeof (datetime), "%b %d %T", &tm) == 0
        || strftime (timezone, sizeof (timezone), "%Z", &tm) == 0
        || strftime (year, sizeof (year), "%Y", &tm) == 0)
        fprintf (fp, "%s ", hdr->timestamp);
    else {
        fprintf (fp,
                 "%s.%06ld %s %s ",
                 datetime,
                 (long)tv.tv_usec,
                 timezone,
                 year);
    }
}

static void make_syslog_prefix (logbuf_t *logbuf, char *buf, size_t size)
{
    if (!logbuf->jobid_path) {
        const char *val = NULL;
        if (attr_get (logbuf->attrs, "jobid-path", &val) == 0)
            logbuf->jobid_path = strdup (val);
    }
    if (!logbuf->username) {
        struct passwd pwd;
        struct passwd *result;
        char pbuf[4096];
        (void)getpwuid_r(geteuid (), &pwd, pbuf, sizeof (pbuf), &result);
        if (result)
            logbuf->username = strdup (result->pw_name);
    }
    if (snprintf (buf,
                  size,
                  "%s@%s",
                  logbuf->username ? logbuf->username : "unknown",
                  logbuf->jobid_path ? logbuf->jobid_path : "unknown") >= size)
        (void)snprintf (buf + size - 4, 4, "...");
}

/* Log a message to syslog.
 */
static void log_syslog (logbuf_t *logbuf, const char *buf, int len)
{
    struct stdlog_header hdr;
    const char *msg;
    size_t msglen;
    char prefix[32];

    make_syslog_prefix (logbuf, prefix, sizeof (prefix));
    if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0) {
        syslog (LOG_INFO, "%s %.*s\n", prefix, len, buf);
    }
    else {
        uint32_t nodeid = strtoul (hdr.hostname, NULL, 10);
        int severity = STDLOG_SEVERITY (hdr.pri);
        syslog (severity,
                "%s %s.%s[%" PRIu32 "]: %.*s\n",
                prefix,
                hdr.appname,
                stdlog_severity_to_string (severity),
                nodeid,
                (int)msglen,
                msg);
    }
}

/* Log a message to 'fp', if non-NULL.
 * Set flags to LOG_FOR_SYSTEMD to suppress timestamp and add <level> prefix.
 */
static void log_fp (FILE *fp, int flags, const char *buf, int len)
{
    struct stdlog_header hdr;
    const char *msg;
    size_t msglen;
    int severity;

    if (fp) {
        if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0) {
            fprintf (fp, "%.*s\n", len, buf);
        }
        else {
            severity = STDLOG_SEVERITY (hdr.pri);
            if ((flags & LOG_FOR_SYSTEMD)) {
                fprintf (fp,
                         "<%d>%s.%s[%s]: %.*s\n",
                         severity,
                         hdr.appname,
                         stdlog_severity_to_string (severity),
                         hdr.hostname,
                         (int)msglen,
                         msg);
            }
            else {
                log_timestamp (fp, &hdr);
                fprintf (fp,
                         "%s.%s[%s]: %.*s\n",
                         hdr.appname,
                         stdlog_severity_to_string (severity),
                         hdr.hostname,
                         (int)msglen,
                         msg);
            }
        }
        fflush (fp);
    }
}

void log_early (const char *buf, int len, void *arg)
{
    attr_t *attrs = arg;
    const char *val;
    int flags = 0;
    int level;
    struct stdlog_header hdr;

    if (attr_get (attrs, "log-stderr-mode", &val) == 0
        && streq (val, "local")) {
        flags = LOG_FOR_SYSTEMD;
    }
    if (getattr_level (attrs, "log-stderr-level", &level) == 0
        && stdlog_decode (buf, len, &hdr, NULL, NULL, NULL, NULL) == 0
        && STDLOG_SEVERITY (hdr.pri) > level)
        return;
    log_fp (stderr, flags, buf, len);
}

static int logbuf_append (logbuf_t *logbuf, const char *buf, int len)
{
    bool logged_stderr = false;
    int rc = 0;
    uint32_t rank = FLUX_NODEID_ANY;
    int severity = LOG_INFO;
    struct stdlog_header hdr;

    /* Fetch this from the attribute hash again for each log entry,
     * in case it changed.
     */
    (void)getattr_level (logbuf->attrs,
                         "log-stderr-level",
                         &logbuf->stderr_level);

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
                flags |= LOG_FOR_SYSTEMD;
            log_fp (stderr, flags, buf, len);
            logged_stderr = true;
        }
        logbuf->recv_local_count++;
    }
    else
        logbuf->recv_remote_count++;

    if (logbuf->rank == 0)
        log_fp (logbuf->f, 0, buf, len);
    else if (severity <= logbuf->forward_level) {
        if (logbuf_forward (logbuf, buf, len) < 0)
            rc = -1;
    }

    if (!logged_stderr
        && severity <= logbuf->stderr_level
        && logbuf->stderr_mode == MODE_LEADER
        && logbuf->rank == 0) {
        log_fp (stderr, 0, buf, len);
    }
    if (logbuf->syslog_enable && severity <= logbuf->syslog_level) {
        log_syslog (logbuf, buf, len);
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
static void append_request_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    logbuf_t *logbuf = arg;
    uint32_t matchtag;
    const char *buf;
    size_t len;

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

static void clear_request_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    logbuf_t *logbuf = arg;

    logbuf_trim (logbuf, 0);
    flux_respond (h, msg, NULL);
    return;
}

static void dmesg_request_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    logbuf_t *logbuf = arg;
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
        struct logbuf_entry *e = NULL;
        list_for_each (&logbuf->ring, e, list) {
            if (flux_respond (h, msg, e->buf) < 0) {
                log_err ("error responding to log.dmesg request");
                goto error;
            }
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
                           "{s:i s:i s:i s:i}",
                           "ring-used", (int)logbuf->ring_count,
                           "count", logbuf->seq,
                           "local", logbuf->recv_local_count,
                           "remote", logbuf->recv_remote_count) < 0)
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
    flux_error_t error;
    logbuf_t *logbuf = logbuf_create ();

    if (!logbuf)
        goto error;
    logbuf->h = h;
    logbuf->rank = rank;
    logbuf->attrs = attrs;
    if (logbuf_register_attrs (logbuf, &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto error;
    }
    if (logbuf->syslog_enable)
        openlog ("flux", LOG_NDELAY | LOG_PID, LOG_USER);
    if (logbuf->filename) {
        FILE *f;
        if (!(f = fopen (logbuf->filename, "a"))) {
            flux_log_error (h, "Error opening logfile %s", logbuf->filename);
            return -1;
        }
        logbuf->f = f;
    }
    if (flux_msg_handler_addvec (h, htab, logbuf, &logbuf->handlers) < 0)
        goto error;
    flux_log_set_redirect (h, logbuf_append_redirect, logbuf);
    flux_log_set_hostname (h, NULL); // use the rank
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
