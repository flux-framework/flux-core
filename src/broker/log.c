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
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/wallclock.h"
#include "src/common/libutil/stdlog.h"

#include "log.h"

/* See descriptions in flux-broker-attributes(7) */
static const int default_ring_size = 1024;
static const int default_forward_level = LOG_DEBUG;
static const int default_critical_level = LOG_CRIT;
static const int default_stderr_level = LOG_ERR;
static const int default_level = LOG_DEBUG;

#define LOGBUF_MAGIC 0xe1e2e3e4
typedef struct {
    int magic;
    flux_t *h;
    uint32_t rank;
    char *filename;
    FILE *f;
    int forward_level;
    int critical_level;
    int stderr_level;
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
    flux_msg_handler_t *w;
    flux_msg_handler_f fun;
    flux_msg_t *msg;
    void *arg;
};

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
                                       flux_t *h, flux_msg_handler_t *w,
                                       const flux_msg_t *msg, void *arg)
{
    struct sleeper *s = calloc (1, sizeof (*s));
    if (!s)
        return NULL;
    s->magic = SLEEPER_MAGIC;
    s->h = h;
    s->w = w;
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
    struct logbuf_entry *e = xzmalloc (sizeof (*e));
    e->buf = xzmalloc (len);
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

static void logbuf_clear (logbuf_t *logbuf, int seq_index)
{
    struct logbuf_entry *e;

    if (seq_index == -1)
        logbuf_trim (logbuf, 0);
    else {
        while ((e = zlist_first (logbuf->buf)) && e->seq <= seq_index) {
            e = zlist_pop (logbuf->buf);
            logbuf_entry_destroy (e);
        }
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
                           flux_msg_handler_t *w, const flux_msg_t *msg,
                           void *arg)
{
    assert (logbuf->magic == LOGBUF_MAGIC);
    struct sleeper *s = sleeper_create (fun, h, w, msg, arg);
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
        e = logbuf_entry_create (buf, len);
        e->seq = logbuf->seq++;
        if (zlist_append (logbuf->buf, e) < 0) {
            logbuf_entry_destroy (e);
            errno = ENOMEM;
            return -1;
        }
        while ((s = zlist_pop (logbuf->sleepers))) {
            s->fun (s->h, s->w, s->msg, s->arg);
            sleeper_destroy (s);
        }
    }
    return 0;
}

static logbuf_t *logbuf_create (void)
{
    logbuf_t *logbuf = xzmalloc (sizeof (*logbuf));
    logbuf->magic = LOGBUF_MAGIC;
    logbuf->forward_level = default_forward_level;
    logbuf->critical_level = default_critical_level;
    logbuf->stderr_level = default_stderr_level;
    logbuf->level = default_level;
    logbuf->ring_size = default_ring_size;
    if (!(logbuf->buf = zlist_new ()))
        oom();
    if (!(logbuf->sleepers = zlist_new ()))
        oom();
    return logbuf;
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
    FILE *f;
    if (logbuf->rank > 0)
        return 0;
    if (!(f = fopen (destination, "a")))
        return -1;
    if (logbuf->filename)
        free (logbuf->filename);
    if (logbuf->f)
        fclose (logbuf->f);
    logbuf->filename = xstrdup (destination);
    logbuf->f = f;
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
    char s[PATH_MAX];
    const char *val;
    int rc = -1;

    /* log-filename
     * Only allowed to be set on rank 0 (ignore initial value on rank > 0).
     * If unset, and persist-directory is set, make it ${persist-directory}/log
     */
    if (logbuf->rank == 0) {
        if (attr_get (attrs, "log-filename", NULL, NULL) < 0
          && attr_get (attrs, "persist-directory", &val, NULL) == 0 && val) {
            if (snprintf (s, sizeof (s), "%s/log", val) >= sizeof (s)) {
                log_err ("log-filename truncated");
                goto done;
            }
            if (attr_add (attrs, "log-filename", s, 0) < 0) {
                log_err ("could not initialize log-filename");
                goto done;
            }
        }
        if (attr_add_active (attrs, "log-filename", 0,
                             attr_get_log, attr_set_log, logbuf) < 0)
            goto done;
        if (attr_add_active (attrs, "log-stderr-level", 0,
                             attr_get_log, attr_set_log, logbuf) < 0)
            goto done;
    } else {
        (void)attr_delete (attrs, "log-filename", true);
        if (attr_add (attrs, "log-filename", NULL, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
        (void)attr_delete (attrs, "log-stderr-level", true);
        if (attr_add (attrs, "log-stderr-level", NULL, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }

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
        if (severity <= logbuf->critical_level) {
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
    if (!logged_stderr && severity <= logbuf->stderr_level && logbuf->rank == 0)
        flux_log_fprint (buf, len, stderr);
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
static void append_request_cb (flux_t *h, flux_msg_handler_t *w,
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
    if (flux_request_decode_raw (msg, NULL, &buf, &len) < 0)
        goto error;
    if (logbuf_append (logbuf, buf, len) < 0)
        goto error;
    if (matchtag != FLUX_MATCHTAG_NONE) {
        if (flux_respond (h, msg, 0, NULL) < 0)
            log_err ("%s: error responding to log request", __FUNCTION__);
    }
    return;
error:
    if (matchtag != FLUX_MATCHTAG_NONE) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            log_err ("%s: error responding to log request", __FUNCTION__);
    }
}

static void clear_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    logbuf_t *logbuf = arg;
    int seq;
    int rc = -1;

    if (flux_request_unpack (msg, NULL, "{ s:i }", "seq", &seq) < 0)
        goto done;
    logbuf_clear (logbuf, seq);
    rc = 0;
done:
    flux_respond (h, msg, rc < 0 ? errno : 0, NULL);
}

static void dmesg_request_cb (flux_t *h, flux_msg_handler_t *w,
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
            if (logbuf_sleepon (logbuf, dmesg_request_cb, h, w, msg, arg) < 0)
                goto error;
            return; /* no reply */
        }
        goto error;
    }
    if (flux_respondf (h, msg, "{ s:i s:s# }",
                       "seq", seq,
                       "buf", buf, len) < 0)
        goto error;
    return;

error:
    flux_respond (h, msg, errno, NULL);
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

static void disconnect_request_cb (flux_t *h, flux_msg_handler_t *w,
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

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "log.append",         append_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "log.clear",          clear_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "log.dmesg",          dmesg_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "log.disconnect",     disconnect_request_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

static void logbuf_finalize (void *arg)
{
    logbuf_t *logbuf = arg;
    logbuf_destroy (logbuf);
    flux_msg_handler_delvec (handlers);
    /* FIXME: need logbuf_unregister_attrs() */
}

static int fake_rank (flux_t *h, uint32_t rank)
{
    char buf[16];
    snprintf (buf, sizeof (buf), "%u", rank);
    return flux_attr_fake (h, "rank", buf, FLUX_ATTRFLAG_IMMUTABLE);
}

int logbuf_initialize (flux_t *h, uint32_t rank, attr_t *attrs)
{
    logbuf_t *logbuf = logbuf_create ();
    if (!logbuf)
        goto error;
    if (logbuf_register_attrs (logbuf, attrs) < 0)
        goto error;
    if (fake_rank (h, rank) < 0)
        goto error;
    if (flux_msg_handler_addvec (h, handlers, logbuf) < 0)
        goto error;
    flux_log_set_appname (h, "broker");
    flux_log_set_redirect (h, logbuf_append_redirect, logbuf);
    logbuf->h = h;
    logbuf->rank = rank;
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
