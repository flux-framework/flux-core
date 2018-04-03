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

/* kz.c - KVS streams */

/* We use a kvs directory to represent a character stream.
 * Blocks are written as sequenced keys (monotonic int) in the directory.
 * Each block is represented as a zio json frame.
 *
 * kz_get (only valid for kz_open KZ_FLAGS_READ):
 * We try to kvs_get '000000' from the stream.  If ESRCH, we either block
 * until that key appears, or if KZ_FLAGS_NONBLOCK, return -1, errno = EAGAIN.
 * Once we have the value, its data is extracted and returned.
 * The next read repeats the above for '000001' and so on.
 * If the value contains an EOF flag, return 0.
 *
 * kz_put (only valid for kz_open KZ_FLAGS_WRITE):
 * Any existing contents are removed.
 * Writing begins at '000000'.  Each kz_put returns either -1 or
 * the number of bytes requested to be written (there are no short writes).
 * A kvs_commit is issued after every kz_put, unless disabled.
 *
 * kz_flush
 * If KZ_FLAGS_WRITE, issues a kvs_commit(), otherwise no-op.
 *
 * kz_close
 * If KZ_FLAGS_WRITE, puts a value containing the EOF flag and issues
 * a kvs_commit(), unless disabled
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <sys/wait.h>
#include <termios.h>
#include <czmq.h>
#include <flux/core.h>

#include "kz.h"

#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libsubprocess/zio.h"

struct kz_struct {
    int flags;
    char *key;
    int key_sz;
    int name_len;
    flux_t *h;
    int seq;
    kz_ready_f ready_cb;
    void *ready_arg;
    bool eof;
    int nprocs;
    char *grpname;
    int fencecount;
    bool watching;
    flux_future_t *lookup_f; // kvs_lookup in progress for kz->seq
    int last_dir_size;
};

static void kz_destroy (kz_t *kz)
{
    if (kz) {
        int saved_errno = errno;
        free (kz->key);
        free (kz->grpname);
        flux_future_destroy (kz->lookup_f);
        free (kz);
        errno = saved_errno;
    }
}

/* Initialize kz->key, kz->key_sz, and kz->name_len.
 * kz->key is allocated to fit a sequenced key written by format_key().
 * The base 'name' is stored.
 */
static int init_key (kz_t *kz, const char *name)
{
    int n;

    if (!name) {
        errno = EINVAL;
        return -1;
    }
    kz->name_len = strlen (name);
    kz->key_sz = kz->name_len + 16; // name.XXXXXX (with \0) is name_len + 8
    if (!(kz->key = malloc (kz->key_sz)))
        return -1;
    n = snprintf (kz->key, kz->key_sz, "%s", name);
    assert (n < kz->key_sz);

    return 0;
}

/* Update kz->key to contain name.<seq> and return it.
 */
static const char *format_key (kz_t *kz, int seq)
{
    int n;

    n = snprintf (kz->key + kz->name_len,
                  kz->key_sz - kz->name_len, ".%.6d", seq);
    assert (n < kz->key_sz - kz->name_len);
    return kz->key;
}

/* Update kz->key to contain just name (the directory) and return it.
 */
static char *clear_key (kz_t *kz)
{
    kz->key[kz->name_len] = '\0';
    return kz->key;
}

kz_t *kz_open (flux_t *h, const char *name, int flags)
{
    kz_t *kz;

    if (!(kz = calloc (1, sizeof (*kz))))
        return NULL;
    if (init_key (kz, name) < 0)
        goto error;

    kz->flags = flags;
    kz->h = h;

    if ((flags & KZ_FLAGS_WRITE)) {
        if (flux_kvs_mkdir (h, name) < 0) // overwrites existing
            goto error;
        if (!(flags & KZ_FLAGS_NOCOMMIT_OPEN)) {
            if (flux_kvs_commit_anon (h, 0) < 0)
                goto error;
        }
    }
    return kz;
error:
    kz_destroy (kz);
    return NULL;
}

static int kz_fence (kz_t *kz)
{
    char *name;
    int rc;
    if (asprintf (&name, "%s.%d", kz->grpname, kz->fencecount++) < 0)
        oom ();
    rc = flux_kvs_fence_anon (kz->h, name, kz->nprocs, 0);
    free (name);
    return rc;
}

kz_t *kz_gopen (flux_t *h, const char *grpname, int nprocs,
               const char *name, int flags)
{
    kz_t *kz;

    if (!(flags & KZ_FLAGS_WRITE) || !grpname || nprocs <= 0) {
        errno = EINVAL;
        return NULL;
    }
    flags |= KZ_FLAGS_NOCOMMIT_OPEN;
    flags |= KZ_FLAGS_NOCOMMIT_CLOSE;
    if (!(kz = kz_open (h, name, flags)))
        return NULL;
    kz->grpname = xstrdup (grpname);
    kz->nprocs = nprocs;
    if (kz_fence (kz) < 0)
        goto error;
    return kz;
error:
    kz_destroy (kz);
    return NULL;
}

static int putnext (kz_t *kz, const char *json_str)
{
    if (!(kz->flags & KZ_FLAGS_WRITE)) {
        errno = EINVAL;
        return -1;
    }
    const char *key = format_key (kz, kz->seq++);
    if (flux_kvs_put (kz->h, key, json_str) < 0)
        return -1;
    if (!(kz->flags & KZ_FLAGS_NOCOMMIT_PUT)) {
        if (flux_kvs_commit_anon (kz->h, 0) < 0)
            return -1;
    }
    return 0;
}

int kz_put_json (kz_t *kz, const char *json_str)
{
    if (!(kz->flags & KZ_FLAGS_RAW)) {
        errno = EINVAL;
        return -1;
    }
    return putnext (kz, json_str);
}

int kz_put (kz_t *kz, char *data, int len)
{
    char *json_str = NULL;
    int rc = -1;

    if (len == 0 || data == NULL || (kz->flags & KZ_FLAGS_RAW)) {
        errno = EINVAL;
        goto done;
    }
    if (!(json_str = zio_json_encode (data, len, false))) {
        errno = EPROTO;
        goto done;
    }
    if (putnext (kz, json_str) < 0)
        goto done;
    rc = len;
done:
    if (json_str)
        free (json_str);
    return rc;
}

/* This function will not block if called once from a kz_ready_f handler since:
 * 1) kvs_watch has already indicated kz->seq is availalbe
 * 2) a kvs_lookup on it has been started
 * 3) the kvs_lookup continuation is what called the kz_ready_f.
 *
 * This function WILL block if called prematurely:
 * If called before step 3, it will block on the KVS response.
 * If called before step 2, it will send the request and block on the response.
 * If called before step 1, it will send the request, block on the response,
 * and may possibly return EAGAIN if kz->seq does not exist yet.
 */
static char *getnext (kz_t *kz)
{
    const char *s;
    const char *key = format_key (kz, kz->seq);
    char *json_str = NULL;

    if (!kz->lookup_f) {
        if (!(kz->lookup_f = flux_kvs_lookup (kz->h, 0, key)))
            return NULL;
    }
    if (flux_kvs_lookup_get (kz->lookup_f, &s) < 0) {
        if (errno == ENOENT)
            errno = EAGAIN;
        flux_future_destroy (kz->lookup_f);
        kz->lookup_f = NULL;
        return NULL;
    }
    if (!(json_str = strdup (s)))
        return NULL;
    flux_future_destroy (kz->lookup_f);
    kz->lookup_f = NULL;
    kz->seq++;
    return json_str;
}

static char *getnext_blocking (kz_t *kz)
{
    const char *key = format_key (kz, kz->seq);
    char *json_str = NULL;

    if (flux_kvs_watch_once (kz->h, key, &json_str) < 0)
        return NULL;
    kz->seq++;
    return json_str;
}

char *kz_get_json (kz_t *kz)
{
    char *json_str;
    if (!(kz->flags & KZ_FLAGS_RAW) || !(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        return NULL;
    }
    if ((kz->flags & KZ_FLAGS_NONBLOCK))
        json_str = getnext (kz);
    else
        json_str = getnext_blocking (kz);
    return json_str;
}

int kz_get (kz_t *kz, char **datap)
{
    char *json_str = NULL;
    char *data;
    int len = -1;

    if (!datap || (kz->flags & KZ_FLAGS_RAW) || !(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        goto done;
    }
    if (kz->eof)
        return 0;
    if ((kz->flags & KZ_FLAGS_NONBLOCK))
        json_str = getnext (kz);
    else
        json_str = getnext_blocking (kz);
    if (!json_str)
        goto done;
    if ((len = zio_json_decode (json_str, (void **) &data, &kz->eof)) < 0) {
        errno = EPROTO;
        goto done;
    }
    *datap = data;
done:
    if (json_str)
        free (json_str);
    return len;
}

int kz_flush (kz_t *kz)
{
    int rc = 0;
    if ((kz->flags & KZ_FLAGS_WRITE))
        rc = flux_kvs_commit_anon (kz->h, 0);
    return rc;
}

int kz_close (kz_t *kz)
{
    int rc = -1;
    char *json_str = NULL;

    if ((kz->flags & KZ_FLAGS_WRITE)) {
        if (!(kz->flags & KZ_FLAGS_RAW)) {
            const char *key = format_key (kz, kz->seq++);
            if (!(json_str = zio_json_encode (NULL, 0, true))) { /* EOF */
                errno = EPROTO;
                goto done;
            }
            if (flux_kvs_put (kz->h, key, json_str) < 0)
                goto done;
        }
        if (!(kz->flags & KZ_FLAGS_NOCOMMIT_CLOSE)) {
            if (flux_kvs_commit_anon (kz->h, 0) < 0)
                goto done;
        }
        if (kz->nprocs > 0 && kz->grpname) {
            if (kz_fence (kz) < 0)
                goto done;
        }
    }
    if (kz->watching) {
        const char *key = clear_key (kz);
        (void)flux_kvs_unwatch (kz->h, key);
        kz->watching = false;
    }
    rc = 0;
done:
    if (json_str)
        free (json_str);
    kz_destroy (kz);
    return rc;
}

static void lookup_continuation (flux_future_t *f, void *arg)
{
    kz_t *kz = arg;

    assert (f == kz->lookup_f);

    flux_log (kz->h, LOG_DEBUG, "%s: got seq=%d", __FUNCTION__, kz->seq);

    if (kz->ready_cb)
        kz->ready_cb (kz, kz->ready_arg);

    if (kz->lookup_f) {
        flux_future_destroy (kz->lookup_f);
        kz->lookup_f = NULL;
    }

    if (kz->seq < kz->last_dir_size) {
        const char *key = format_key (kz, kz->seq);
        if (!(kz->lookup_f = flux_kvs_lookup (kz->h, 0, key))) {
            flux_log_error (kz->h, "%s: flux_kvs_lookup", __FUNCTION__);
            return;
        }
        if (flux_future_then (kz->lookup_f, -1., lookup_continuation, kz) < 0) {
            flux_log_error (kz->h, "%s: flux_future_then", __FUNCTION__);
            flux_future_destroy (kz->lookup_f);
            kz->lookup_f = NULL;
            return;
        }
    }
}

static int kvswatch_cb (const char *dir_key, flux_kvsdir_t *dir,
                        void *arg, int errnum)
{
    kz_t *kz = arg;

    if (errnum == ENOENT)
        kz->last_dir_size = 0;
    else if (errnum == 0)
        kz->last_dir_size = flux_kvsdir_get_size (dir);
    else {
        flux_log (kz->h, LOG_ERR, "%s: %s", __FUNCTION__, strerror (errnum));
        return -1;
    }

    if (kz->lookup_f == NULL && kz->seq < kz->last_dir_size) {
        const char *key = format_key (kz, kz->seq);
        if (!(kz->lookup_f = flux_kvs_lookup (kz->h, 0, key))) {
            flux_log_error (kz->h, "%s: flux_kvs_lookup", __FUNCTION__);
            return -1;
        }
        if (flux_future_then (kz->lookup_f, -1., lookup_continuation, kz) < 0) {
            flux_log_error (kz->h, "%s: flux_future_then", __FUNCTION__);
            flux_future_destroy (kz->lookup_f);
            kz->lookup_f = NULL;
            return -1;
        }
        flux_log (kz->h, LOG_DEBUG, "%s: lookup seq=%d", __FUNCTION__, kz->seq);
    }
    return 0;
}

int kz_set_ready_cb (kz_t *kz, kz_ready_f ready_cb, void *arg)
{
    if (!(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        return -1;
    }
    kz->ready_cb = ready_cb;
    kz->ready_arg = arg;
    const char *key = clear_key (kz);
    if (kz->ready_cb != NULL && !kz->watching) {
        if (flux_kvs_watch_dir (kz->h, kvswatch_cb, kz, "%s", key) < 0)
            return -1;
        kz->watching = true;
    }
    if (kz->ready_cb == NULL && kz->watching) {
        if (flux_kvs_unwatch (kz->h, key) < 0)
            return -1;
        kz->watching = false;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
