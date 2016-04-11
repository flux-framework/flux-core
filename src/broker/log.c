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
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

#include "log.h"

static const int default_ring_size = 1024;
static const int default_forward_level = LOG_DEBUG;
static const int default_critical_level = LOG_CRIT;
static const int default_stderr_level = LOG_ERR;

#define LOG_MAGIC 0xe1e2e3e4
struct log_struct {
    int magic;
    flux_t h;
    uint32_t rank;
    char *filename;
    FILE *f;
    int forward_level;
    int critical_level;
    int stderr_level;
    zlist_t *buf;
    int ring_size;
    int seq;
    zlist_t *sleepers;
};

struct log_entry {
    char *facility;
    int level;
    uint32_t rank;
    struct timeval tv;
    char *message;
    int seq;
    JSON obj;
};

struct sleeper {
    log_sleeper_f fun;
    flux_msg_t *msg;
    void *arg;
};

static void sleeper_destroy (struct sleeper *s)
{
    if (s) {
        flux_msg_destroy (s->msg);
        free (s);
    }
}

static struct sleeper *sleeper_create (log_sleeper_f fun, flux_msg_t *msg,
                                       void *arg)
{
    struct sleeper *s = xzmalloc (sizeof (*s));
    s->fun = fun;
    s->msg = msg;
    s->arg = arg;
    return s;
}

static void log_entry_destroy (struct log_entry *e)
{
    if (e) {
        if (e->facility)
            free (e->facility);
        if (e->message)
            free (e->message);
        Jput (e->obj);
        free (e);
    }
}

static struct log_entry *log_entry_create (const char *facility, int level,
                                           uint32_t rank, struct timeval tv,
                                           const char *message)
{
    struct log_entry *e = xzmalloc (sizeof (*e));
    e->facility = xstrdup (facility);
    e->level = level;
    e->rank = rank;
    e->tv = tv;
    e->message = xstrdup (message);
    return e;
}

static void log_buf_trim (log_t *log, int size)
{
    assert (log->magic == LOG_MAGIC);
    struct log_entry *e;
    while (zlist_size (log->buf) > size) {
        e = zlist_pop (log->buf);
        log_entry_destroy (e);
    }
}

void log_buf_clear (log_t *log, int seq_index)
{
    struct log_entry *e;

    if (seq_index == -1)
        log_buf_trim (log, 0);
    else {
        while ((e = zlist_first (log->buf)) && e->seq <= seq_index) {
            e = zlist_pop (log->buf);
            log_entry_destroy (e);
        }
    }
}

const char *log_buf_get (log_t *log, int seq_index, int *seq)
{
    struct log_entry *e = zlist_first (log->buf);

    while (e && e->seq <= seq_index)
        e = zlist_next (log->buf);
    if (!e) {
        errno = ENOENT;
        goto done;
    }
    if (!e->obj) {
        e->obj = Jnew ();
        Jadd_str (e->obj, "facility", e->facility);
        Jadd_int (e->obj, "level", e->level);
        Jadd_int (e->obj, "rank", e->rank);
        Jadd_int (e->obj, "timestamp_usec", e->tv.tv_usec);
        Jadd_int (e->obj, "timestamp_sec", e->tv.tv_sec);
        Jadd_str (e->obj, "message", e->message);
    }
    *seq = e->seq;
done:
    return e ? Jtostr (e->obj) : NULL;
}

int log_buf_sleepon (log_t *log, log_sleeper_f fun, flux_msg_t *msg, void *arg)
{
    assert (log->magic == LOG_MAGIC);
    struct sleeper *s = sleeper_create (fun, msg, arg);
    if (zlist_append (log->sleepers, s) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void log_buf_disconnect (log_t *log, const char *uuid)
{
    assert (log->magic == LOG_MAGIC);
    struct sleeper *s = zlist_first (log->sleepers);
    while (s) {
        char *sender = NULL;
        if (flux_msg_get_route_first (s->msg, &sender) == 0 && sender) {
            if (!strcmp (uuid, sender)) {
                zlist_remove (log->sleepers, s);
                sleeper_destroy (s);
            }
            free (sender);
        }
        s = zlist_next (log->sleepers);
    }
}

static int log_buf_append (log_t *log, const char *facility, int level,
                           uint32_t rank, struct timeval tv,
                           const char *message)
{
    assert (log->magic == LOG_MAGIC);
    struct log_entry *e;
    struct sleeper *s;

    if (log->ring_size > 0) {
        log_buf_trim (log, log->ring_size - 1);
        e = log_entry_create (facility, level, rank, tv, message);
        e->seq = log->seq++;
        if (zlist_append (log->buf, e) < 0) {
            log_entry_destroy (e);
            errno = ENOMEM;
            return -1;
        }
        while ((s = zlist_pop (log->sleepers))) {
            s->fun (s->msg, s->arg);
            sleeper_destroy (s);
        }
    }
    return 0;
}

log_t *log_create (void)
{
    log_t *log = xzmalloc (sizeof (*log));
    log->magic = LOG_MAGIC;
    log->forward_level = default_forward_level;
    log->critical_level = default_critical_level;
    log->stderr_level = default_stderr_level;
    log->ring_size = default_ring_size;
    if (!(log->buf = zlist_new ()))
        oom();
    if (!(log->sleepers = zlist_new ()))
        oom();
    return log;
}

void log_destroy (log_t *log)
{
    if (log) {
        assert (log->magic == LOG_MAGIC);
        log->magic = ~LOG_MAGIC;
        if (log->buf) {
            log_buf_trim (log, 0);
            zlist_destroy (&log->buf);
        }
        if (log->sleepers) {
            struct sleeper *s;
            while ((s = zlist_pop (log->sleepers)))
                sleeper_destroy (s);
            zlist_destroy (&log->sleepers);
        }
        if (log->f)
            (void)fclose (log->f);
        if (log->filename)
            free (log->filename);
        free (log);
    }
}

void log_set_flux (log_t *log, flux_t h)
{
    assert (log->magic == LOG_MAGIC);
    log->h = h;
}

void log_set_rank (log_t *log, uint32_t rank)
{
    log->rank = rank;
}

static int log_set_forward_level (log_t *log, int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        errno = EINVAL;
        return -1;
    }
    log->forward_level = level;
    return 0;
}

static int log_set_critical_level (log_t *log, int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        errno = EINVAL;
        return -1;
    }
    log->critical_level = level;
    return 0;
}

static int log_set_stderr_level (log_t *log, int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        errno = EINVAL;
        return -1;
    }
    log->stderr_level = level;
    return 0;
}

static int log_set_ring_size (log_t *log, int size)
{
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }
    log_buf_trim (log, size);
    log->ring_size = size;
    return 0;
}

/* Set the log filename (rank 0 only).
 * Allow other ranks to try to set this without effect
 * so that the same broker options can be used across a session.
 */
static int log_set_filename (log_t *log, const char *destination)
{
    FILE *f;
    if (log->rank > 0)
        return 0;
    if (!(f = fopen (destination, "a")))
        return -1;
    if (log->filename)
        free (log->filename);
    if (log->f)
        fclose (log->f);
    log->filename = xstrdup (destination);
    log->f = f;
    return 0;
}

static int attr_get_log (const char *name, const char **val, void *arg)
{
    log_t *log = arg;
    static char s[32];
    int n, rc = -1;

    if (!strcmp (name, "log-forward-level")) {
        n = snprintf (s, sizeof (s), "%d", log->forward_level);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-critical-level")) {
        n = snprintf (s, sizeof (s), "%d", log->critical_level);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-stderr-level")) {
        n = snprintf (s, sizeof (s), "%d", log->stderr_level);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-ring-size")) {
        n = snprintf (s, sizeof (s), "%d", log->ring_size);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-ring-used")) {
        n = snprintf (s, sizeof (s), "%d", (int)zlist_size (log->buf));
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-count")) {
        n = snprintf (s, sizeof (s), "%d", log->seq);
        assert (n < sizeof (s));
        *val = s;
    } else if (!strcmp (name, "log-filename")) {
        *val = log->filename;
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
    log_t *log = arg;
    int rc = -1;

    if (!strcmp (name, "log-forward-level")) {
        int level = strtol (val, NULL, 10);
        if (log_set_forward_level (log, level) < 0)
            goto done;
    } else if (!strcmp (name, "log-critical-level")) {
        int level = strtol (val, NULL, 10);
        if (log_set_critical_level (log, level) < 0)
            goto done;
    } else if (!strcmp (name, "log-stderr-level")) {
        int level = strtol (val, NULL, 10);
        if (log_set_stderr_level (log, level) < 0)
            goto done;
    } else if (!strcmp (name, "log-ring-size")) {
        int size = strtol (val, NULL, 10);
        if (log_set_ring_size (log, size) < 0)
            goto done;
    } else if (!strcmp (name, "log-filename")) {
        if (log_set_filename (log, val) < 0)
            goto done;
    } else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int log_register_attrs (log_t *log, attr_t *attrs)
{
    char s[PATH_MAX];
    const char *val;
    int rc = -1;

    /* log-filename
     * Only allowed to be set on rank 0 (ignore initial value on rank > 0).
     * If unset, and persist-directory is set, make it ${persist-directory}/log
     */
    if (log->rank == 0) {
        if (attr_get (attrs, "log-filename", NULL, NULL) < 0
          && attr_get (attrs, "persist-directory", &val, NULL) == 0 && val) {
            if (snprintf (s, sizeof (s), "%s/log", val) >= sizeof (s)) {
                err ("log-filename truncated");
                goto done;
            }
            if (attr_add (attrs, "log-filename", s, 0) < 0) {
                err ("could not initialize log-filename");
                goto done;
            }
        }
        if (attr_add_active (attrs, "log-filename", 0,
                             attr_get_log, attr_set_log, log) < 0)
            goto done;
        if (attr_add_active (attrs, "log-stderr-level", 0,
                             attr_get_log, attr_set_log, log) < 0)
            goto done;
    } else {
        (void)attr_delete (attrs, "log-filename", true);
        if (attr_add (attrs, "log-filename", NULL, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
        (void)attr_delete (attrs, "log-stderr-level", true);
        if (attr_add (attrs, "log-stderr-level", NULL, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }

    if (attr_add_active (attrs, "log-forward-level", 0,
                         attr_get_log, attr_set_log, log) < 0)
        goto done;
    if (attr_add_active (attrs, "log-critical-level", 0,
                         attr_get_log, attr_set_log, log) < 0)
        goto done;
    if (attr_add_active (attrs, "log-ring-size", 0,
                         attr_get_log, attr_set_log, log) < 0)
        goto done;
    if (attr_add_active (attrs, "log-ring-used", 0,
                         attr_get_log, NULL, log) < 0)
        goto done;
    if (attr_add_active (attrs, "log-count", 0,
                         attr_get_log, NULL, log) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int log_forward (log_t *log, const char *facility, int level, uint32_t rank,
                 struct timeval tv, const char *message)
{
    assert (log->magic == LOG_MAGIC);
    JSON o = Jnew ();
    flux_rpc_t *rpc = NULL;
    int rc = -1;

    Jadd_str (o, "facility", facility);
    Jadd_int (o, "level", level);
    Jadd_int (o, "rank", rank);
    Jadd_int (o, "timestamp_usec", tv.tv_usec);
    Jadd_int (o, "timestamp_sec", tv.tv_sec);
    Jadd_str (o, "message", message);
    if (!(rpc = flux_rpc (log->h, "cmb.log", Jtostr (o), FLUX_NODEID_UPSTREAM,
                                                         FLUX_RPC_NORESPONSE)))
        goto done;
    rc = 0;
done:
    Jput (o);
    flux_rpc_destroy (rpc);
    return rc;
}

int log_append (log_t *log, const char *facility, int level, uint32_t rank,
                struct timeval tv, const char *message)
{
    assert (log->magic == LOG_MAGIC);
    bool logged_stderr = false;
    int rc = 0;

    if (rank == log->rank) {
        if (log_buf_append (log, facility, level, rank, tv, message) < 0)
            rc = -1;
        if (level <= log->critical_level) {
            flux_log_fprint (facility, level, rank, tv, message, stderr);
            logged_stderr = true;
        }
    }
    if (level <= log->forward_level) {
        if (log->rank == 0) {
            flux_log_fprint (facility, level, rank, tv, message, log->f);
        } else {
            if (log_forward (log, facility, level, rank, tv, message) < 0)
                rc = -1;
        }
    }
    if (!logged_stderr && level <= log->stderr_level && log->rank == 0)
        flux_log_fprint (facility, level, rank, tv, message, stderr);
    return rc;
}

int log_append_json (log_t *log, const char *json_str)
{
    assert (log->magic == LOG_MAGIC);
    JSON in = NULL;
    const char *facility, *message;
    int level, rank, tv_usec, tv_sec;
    struct timeval tv;
    int rc = -1;

    if (!(in = Jfromstr (json_str))
            || !Jget_str (in, "facility", &facility)
            || !Jget_int (in, "level", &level)
            || !Jget_int (in, "rank", &rank)
            || !Jget_int (in, "timestamp_usec", &tv_usec)
            || !Jget_int (in, "timestamp_sec", &tv_sec)
            || !Jget_str (in, "message", &message)) {
        errno = EPROTO;
        goto done;
    }
    tv.tv_sec = tv_sec;
    tv.tv_usec = tv_usec;
    if (log_append (log, facility, level, rank, tv, message) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    return rc;
}

void log_append_redirect (const char *facility, int level, uint32_t rank,
                          struct timeval tv, const char *message, void *arg)
{
    log_t *log = arg;
    assert (log->magic == LOG_MAGIC);
    (void)log_append (log, facility, level, rank, tv, message);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
