/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"

#include "log.h"

typedef enum { MODE_LEADER, MODE_LOCAL } stderr_mode_t;

/* See descriptions in flux-broker-attributes(7) */
static const int default_ring_size = 1024;
static const int default_forward_level = LOG_DEBUG;
static const int default_critical_level = LOG_CRIT;
static const int default_stderr_level = LOG_ERR;
static const stderr_mode_t default_stderr_mode = MODE_LEADER;
static const int default_level = LOG_DEBUG;

#define LOGBUF_MAGIC 0xe1e2e3e4
typedef struct {
    int magic;
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
    zlist_t *sleepers;
} logbuf_t;

struct logbuf_entry {
    char *buf;
    int len;
    int seq;
};

#define SLEEPER_MAGIC 0xe4e3e2e1
struct sleeper {
    int magic;
    flux_t *h;
    flux_msg_handler_t *mh;
    flux_msg_handler_f fun;
    flux_msg_t *msg;
    void *arg;
};

void logbuf_destroy (logbuf_t *logbuf);

static void sleeper_destroy (struct sleeper *s)
{
    if (s) {
        assert (s->magic == SLEEPER_MAGIC);
        flux_msg_destroy (s->msg);
        s->magic =~ SLEEPER_MAGIC;
        free (s);
    }
}

static struct sleeper *sleeper_create (flux_msg_handler_f fun,
                                       flux_t *h, flux_msg_handler_t *mh,
                                       const flux_msg_t *msg, void *arg)
{
    struct sleeper *s = calloc (1, sizeof (*s));
    if (!s)
        return NULL;
    s->magic = SLEEPER_MAGIC;
    s->h = h;
    s->mh = mh;
    s->fun = fun;
    s->arg = arg;
    if (!(s->msg = flux_msg_copy (msg, true))) {
        sleeper_destroy (s);
        return NULL;
    }
    return s;
}

static void logbuf_entry_destroy (struct logbuf_entry *e)
{
    if (e) {
        if (e->buf)
            free (e->buf);
        free (e);
    }
}

static struct logbuf_entry *logbuf_entry_create (const char *buf, int len)
{
    struct logbuf_entry *e = calloc (1, sizeof (*e));
    if (!e)
        return NULL;
    if (!(e->buf = calloc (1, len))) {
        free (e);
        return NULL;
    }
    memcpy (e->buf, buf, len);
    e->len = len;
    return e;
}

static void logbuf_trim (logbuf_t *logbuf, int size)
{
    assert (logbuf->magic == LOGBUF_MAGIC);
    struct logbuf_entry *e;
    while (zlist_size (logbuf->buf) > size) {
        e = zlist_pop (logbuf->buf);
        logbuf_entry_destroy (e);
    }
}

static int logbuf_get (logbuf_t *logbuf, int seq_index, int *seq,
                       const char **buf, int *len)
{
    struct logbuf_entry *e = zlist_first (logbuf->buf);

    while (e && e->seq <= seq_index)
        e = zlist_next (logbuf->buf);
    if (!e) {
        errno = ENOENT;
        return -1;
    }
    if (seq)
        *seq = e->seq;
    if (buf)
        *buf = e->buf;
    if (len)
        *len = e->len;
    return 0;
}

static int logbuf_sleepon (logbuf_t *logbuf, flux_msg_handler_f fun, flux_t *h,
                           flux_msg_handler_t *mh, const flux_msg_t *msg,
                           void *arg)
{
    assert (logbuf->magic == LOGBUF_MAGIC);
    struct sleeper *s = sleeper_create (fun, h, mh, msg, arg);
    if (!s)
        return -1;
    if (zlist_append (logbuf->sleepers, s) < 0) {
        sleeper_destroy (s);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int append_new_entry (logbuf_t *logbuf, const char *buf, int len)
{
    assert (logbuf->magic == LOGBUF_MAGIC);
    struct logbuf_entry *e;
    struct sleeper *s;

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
        while ((s = zlist_pop (logbuf->sleepers))) {
            s->fun (s->h, s->mh, s->msg, s->arg);
            sleeper_destroy (s);
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
    logbuf->magic = LOGBUF_MAGIC;
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
    if (!(logbuf->sleepers = zlist_new ())) {
        errno = ENOMEM;
        goto cleanup;
    }
    return logbuf;
cleanup:
    logbuf_destroy (logbuf);
    return NULL;
}

void logbuf_destroy (logbuf_t *logbuf)
{
    if (logbuf) {
        assert (logbuf->magic == LOGBUF_MAGIC);
        if (logbuf->buf) {
            logbuf_trim (logbuf, 0);
            zlist_destroy (&logbuf->buf);
        }
        if (logbuf->sleepers) {
            struct sleeper *s;
            while ((s = zlist_pop (logbuf->sleepers)))
                sleeper_destroy (s);
            zlist_destroy (&logbuf->sleepers);
        }
        if (logbuf->f)
            (void)fclose (logbuf->f);
        if (logbuf->filename)
            free (logbuf->filename);
        logbuf->magic = ~LOGBUF_MAGIC;
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

static int attr_get_log (const char *name, const char **val, void *arg)
{
    logbuf_t *logbuf = arg;
    static char s[32];
    int n, rc = -1;

    if (!strcmp (name, "log-forward-level")) {
        n = snprintf (s, sizeof (s), "%d", logbuf->forward_level);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-critical-level")) {
        n = snprintf (s, sizeof (s), "%d", logbuf->critical_level);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-stderr-level")) {
        n = snprintf (s, sizeof (s), "%d", logbuf->stderr_level);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-stderr-mode")) {
        n = snprintf (s, sizeof (s), "%s",
                      logbuf->stderr_mode == MODE_LEADER ? "leader" : "local");
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-ring-size")) {
        n = snprintf (s, sizeof (s), "%d", logbuf->ring_size);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-ring-used")) {
        n = snprintf (s, sizeof (s), "%zd", zlist_size (logbuf->buf));
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-count")) {
        n = snprintf (s, sizeof (s), "%d", logbuf->seq);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-filename")) {
        *val = logbuf->filename;
    } else if (!strcmp (name, "log-level")) {
        n = snprintf (s, sizeof (s), "%d", logbuf->level);
        assert (n < sizeof (s));
        *val = s;
    } else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static int attr_set_log (const char *name, const char *val, void *arg)
{
    logbuf_t *logbuf = arg;
    int rc = -1;

    if (!strcmp (name, "log-forward-level")) {
        int level = strtol (val, NULL, 10);
        if (logbuf_set_forward_level (logbuf, level) < 0)
            goto done;
    } else if (!strcmp (name, "log-critical-level")) {
        int level = strtol (val, NULL, 10);
        if (logbuf_set_critical_level (logbuf, level) < 0)
            goto done;
    } else if (!strcmp (name, "log-stderr-level")) {
        int level = strtol (val, NULL, 10);
        if (logbuf_set_stderr_level (logbuf, level) < 0)
            goto done;
    } else if (!strcmp (name, "log-stderr-mode")) {
        if (!strcmp (val, "leader"))
            logbuf->stderr_mode = MODE_LEADER;
        else if (!strcmp (val, "local"))
            logbuf->stderr_mode = MODE_LOCAL;
        else {
            errno = EINVAL;
            goto done;
        }
    } else if (!strcmp (name, "log-ring-size")) {
        int size = strtol (val, NULL, 10);
        if (logbuf_set_ring_size (logbuf, size) < 0)
            goto done;
    } else if (!strcmp (name, "log-filename")) {
        if (logbuf_set_filename (logbuf, val) < 0)
            goto done;
    } else if (!strcmp (name, "log-level")) {
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
        if (attr_add (attrs, "log-filename", NULL, FLUX_ATTRFLAG_IMMUTABLE) < 0)
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
    if (attr_add_active (attrs, "log-ring-used", 0,
                         attr_get_log, NULL, logbuf) < 0)
        goto done;
    if (attr_add_active (attrs, "log-count", 0,
                         attr_get_log, NULL, logbuf) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int logbuf_forward (logbuf_t *logbuf, const char *buf, int len)
{
    assert (logbuf->magic == LOGBUF_MAGIC);

    flux_future_t *f;
    if (!(f = flux_rpc_raw (logbuf->h, "log.append", buf, len,
                              FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

/* Log a message to stderr without timestamp.
 * This assumes a timestamp is added externally, e.g. by the systemd journal.
 */
static void log_stderr (const char *buf, int len)
{
    struct stdlog_header hdr;
    const char *msg;
    int msglen, severity;
    uint32_t nodeid;

    if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
        fprintf (stderr, "%.*s\n", len, buf);
    else {
        nodeid = strtoul (hdr.hostname, NULL, 10);
        severity = STDLOG_SEVERITY (hdr.pri);
        fprintf (stderr, "%s.%s[%" PRIu32 "]: %.*s\n",
                 hdr.appname,
                 stdlog_severity_to_string (severity),
                 nodeid,
                 msglen, msg);
    }
}

static int logbuf_append (logbuf_t *logbuf, const char *buf, int len)
{
    assert (logbuf->magic == LOGBUF_MAGIC);
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
            if (logbuf->stderr_mode == MODE_LOCAL)
                log_stderr (buf, len);
            else
                flux_log_fprint (buf, len, stderr);
            logged_stderr = true;
        }
    }
    if (severity <= logbuf->forward_level) {
        if (logbuf->rank == 0) {
            flux_log_fprint (buf, len, logbuf->f);
        } else {
            if (logbuf_forward (logbuf, buf, len) < 0)
                rc = -1;
        }
    }
    if (!logged_stderr && severity <= logbuf->stderr_level
                       && logbuf->stderr_mode == MODE_LEADER
                       && logbuf->rank == 0) {
            flux_log_fprint (buf, len, stderr);
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
    const char *buf;
    int len;
    int seq, follow;

    if (flux_request_unpack (msg, NULL, "{ s:i s:b }",
                             "seq", &seq,
                             "follow", &follow) < 0)
        goto error;
    if (logbuf_get (logbuf, seq, &seq, &buf, &len) < 0) {
        if (follow && errno == ENOENT) {
            if (logbuf_sleepon (logbuf, dmesg_request_cb, h, mh, msg, arg) < 0)
                goto error;
            return; /* no reply */
        }
        goto error;
    }
    flux_respond_pack (h, msg, "{ s:i s:s# }",
                               "seq", seq,
                               "buf", buf, len);
    return;

error:
    flux_respond_error (h, msg, errno, NULL);
}

static int cmp_sender (flux_msg_t *msg, const char *uuid)
{
    char *sender = NULL;
    int rc = 0;

    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto done;
    if (!sender || strcmp (sender, uuid) != 0)
        goto done;
    rc = 1;
done:
    free (sender);
    return rc;
}

static void disconnect_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    logbuf_t *logbuf = arg;
    char *sender = NULL;
    struct sleeper *s;
    zlist_t *tmp = NULL;

    assert (logbuf->magic == LOGBUF_MAGIC);
    if (flux_msg_get_route_first (msg, &sender) < 0 || !sender)
        goto done;
    s = zlist_first (logbuf->sleepers);
    while (s) {
        assert (s->magic == SLEEPER_MAGIC);
        if (cmp_sender (s->msg, sender)) {
            if (!tmp && !(tmp = zlist_new ()))
                goto done;
            if (zlist_append (tmp, s) < 0)
                goto done;
        }
        s = zlist_next (logbuf->sleepers);
    }
    if (tmp) {
        while ((s = zlist_pop (tmp))) {
            zlist_remove (logbuf->sleepers, s);
            sleeper_destroy (s);
        }
    }
done:
    free (sender);
    zlist_destroy (&tmp);
    /* no response */
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "log.append",         append_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.clear",          clear_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.dmesg",          dmesg_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "log.disconnect",     disconnect_request_cb, 0 },
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
