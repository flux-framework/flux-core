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
#include "src/common/libzio/zio.h"

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
    bool watching;
    flux_future_t *lookup_f; // kvs_lookup in progress for kz->seq
    int last_dir_size;
    int saved_errnum;
    bool saved_errnum_valid;
};

static int lookup_next (kz_t *kz);

static void kz_destroy (kz_t *kz)
{
    if (kz) {
        int saved_errno = errno;
        free (kz->key);
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

static void errnum_save (kz_t *kz, int errnum)
{
    if (!kz->saved_errnum_valid) {
        kz->saved_errnum = errnum;
        kz->saved_errnum_valid = true;
    }
}

static int errnum_check (kz_t *kz)
{
    if (kz->saved_errnum_valid) {
        errno = kz->saved_errnum;
        return -1;
    }
    return 0;
}

/* aukey shared between wreck, lua, and kz */
static const char *kz_default_txn_auxkey = "flux::wreck_lua_kz_txn";
static flux_kvs_txn_t *kz_kvs_get_default_txn (flux_t *h)
{
    flux_kvs_txn_t *txn = NULL;

    assert (h);

    if (!(txn = flux_aux_get (h, kz_default_txn_auxkey))) {
        if (!(txn = flux_kvs_txn_create ()))
            goto done;
        flux_aux_set (h, kz_default_txn_auxkey,
                      txn, (flux_free_f)flux_kvs_txn_destroy);
    }
 done:
    return txn;
}

static void kz_kvs_clear_default_txn (flux_t *h)
{
    flux_aux_set (h, kz_default_txn_auxkey, NULL, NULL);
}

static int kz_kvs_commit (flux_t *h)
{
    flux_kvs_txn_t *txn;
    flux_future_t *f = NULL;
    int rv = -1;

    if (!(txn = kz_kvs_get_default_txn (h)))
        goto error;
    if (!(f = flux_kvs_commit (h, 0, txn)))
        goto error;
    if (flux_future_get (f, NULL) < 0) {
        int saved_errno = errno;
        kz_kvs_clear_default_txn (h);
        errno = saved_errno;
        goto error;
    }
    kz_kvs_clear_default_txn (h);
    rv = 0;
error:
    flux_future_destroy (f);
    return rv;
}

kz_t *kz_open (flux_t *h, const char *name, int flags)
{
    kz_t *kz;

    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(kz = calloc (1, sizeof (*kz))))
        return NULL;
    if (init_key (kz, name) < 0)
        goto error;

    kz->flags = flags;
    kz->h = h;

    if ((flags & KZ_FLAGS_WRITE)) {
        flux_kvs_txn_t *txn;
        if (!(txn = kz_kvs_get_default_txn (h)))
            goto error;
        if (flux_kvs_txn_mkdir (txn, 0, name) < 0) // overwrites existing
            goto error;
        if (!(flags & KZ_FLAGS_NOCOMMIT_OPEN)) {
            if (kz_kvs_commit (h) < 0)
                goto error;
        }
    }
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
    flux_kvs_txn_t *txn;
    if (!(txn = kz_kvs_get_default_txn (kz->h)))
        return -1;
    if (flux_kvs_txn_put (txn, 0, key, json_str) < 0)
        return -1;
    if (!(kz->flags & KZ_FLAGS_NOCOMMIT_PUT)) {
        if (kz_kvs_commit (kz->h) < 0)
            return -1;
    }
    return 0;
}

int kz_put_json (kz_t *kz, const char *json_str)
{
    if (!kz || !json_str || !(kz->flags & KZ_FLAGS_RAW)) {
        errno = EINVAL;
        return -1;
    }
    return putnext (kz, json_str);
}

int kz_put (kz_t *kz, char *data, int len)
{
    char *json_str = NULL;
    int rc = -1;

    if (!kz || len == 0 || data == NULL || (kz->flags & KZ_FLAGS_RAW)) {
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
    int saved_errno;

    if (!kz || !(kz->flags & KZ_FLAGS_RAW) || !(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        return NULL;
    }
    if (errnum_check (kz) < 0)
        return NULL;
    if ((kz->flags & KZ_FLAGS_NONBLOCK))
        json_str = getnext (kz);
    else
        json_str = getnext_blocking (kz);
    if (!json_str)
        return NULL;
    if (zio_json_decode (json_str, NULL, &kz->eof) < 0) { // update kz->eof
        errno = EPROTO;
        goto error;
    }
    return json_str;
error:
    saved_errno = errno;
    free (json_str);
    errno = saved_errno;
    return NULL;
}

int kz_get (kz_t *kz, char **datap)
{
    char *json_str = NULL;
    char *data;
    int len;
    int saved_errno;

    if (!kz || !datap || (kz->flags & KZ_FLAGS_RAW)
                      || !(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        return -1;
    }
    if (errnum_check (kz) < 0)
        return -1;
    if (kz->eof)
        return 0;
    if ((kz->flags & KZ_FLAGS_NONBLOCK) || (kz->flags & KZ_FLAGS_NOFOLLOW))
        json_str = getnext (kz);
    else
        json_str = getnext_blocking (kz);
    if (!json_str) {
        if ((kz->flags & KZ_FLAGS_NOFOLLOW) && (errno == EAGAIN)) {
            kz->eof = true;
            return 0;
        } else
            return -1;
    }
    if ((len = zio_json_decode (json_str, (void **) &data, &kz->eof)) < 0) {
        errno = EPROTO;
        goto error;
    }
    *datap = data;
    return len;
error:
    saved_errno = errno;
    free (json_str);
    errno = saved_errno;
    return -1;
}

int kz_flush (kz_t *kz)
{
    if (!kz || !(kz->flags & KZ_FLAGS_WRITE)) {
        errno = EINVAL;
        return -1;
    }
    return kz_kvs_commit (kz->h);
}

int kz_close (kz_t *kz)
{
    char *json_str = NULL;
    int saved_errno;

    if (!kz)
        return 0;

    if ((kz->flags & KZ_FLAGS_WRITE)) {
        if (!(kz->flags & KZ_FLAGS_RAW)) {
            const char *key = format_key (kz, kz->seq++);
            flux_kvs_txn_t *txn;
            if (!(json_str = zio_json_encode (NULL, 0, true))) { /* EOF */
                errno = EPROTO;
                goto error;
            }
            if (!(txn = kz_kvs_get_default_txn (kz->h)))
                goto error;
            if (flux_kvs_txn_put (txn, 0, key, json_str) < 0)
                goto error;
        }
        if (!(kz->flags & KZ_FLAGS_NOCOMMIT_CLOSE)) {
            if (kz_kvs_commit (kz->h) < 0)
                goto error;
        }
    }
    if (kz->watching) {
        const char *key = clear_key (kz);
        (void)flux_kvs_unwatch (kz->h, key);
        kz->watching = false;
    }
    if (errnum_check (kz) < 0)
        goto error;
    kz_destroy (kz);
    free (json_str);
    return 0;
error:
    saved_errno = errno;
    free (json_str);
    kz_destroy (kz);
    errno = saved_errno;
    return -1;
}

static void kz_unwatch (kz_t *kz)
{
    if (kz->watching) {
        const char *key = clear_key (kz);
        if (flux_kvs_unwatch (kz->h, key) >= 0)
            kz->watching = false;
    }
}

/* Handle response for lookup of next block (kz->seq).
 * Notify user, who should call kz_get() or kz_get_json() to consume it.
 */
static void lookup_continuation (flux_future_t *f, void *arg)
{
    kz_t *kz = arg;

    assert (f == kz->lookup_f);

    if (kz->ready_cb)
        kz->ready_cb (kz, kz->ready_arg);
    if (kz->lookup_f != NULL) {
        flux_log (kz->h, LOG_ERR, "%s: %s unclaimed data - fatal error",
                  __FUNCTION__, flux_kvs_lookup_get_key (f));
        errno = EINVAL;
        errnum_save (kz, errno);
        flux_reactor_stop_error (flux_get_reactor (kz->h));
    }
    /* If last block of this stream has been handled,
     * disable the KVS watcher (if any) as we're done.
     * Otherwise, go get the next block.
     */
    if (kz->eof)
        kz_unwatch (kz);
    else if (lookup_next (kz) < 0)
        goto error;
    return;
error:
    errnum_save (kz, errno);
    if (kz->ready_cb)
        kz->ready_cb (kz, kz->ready_arg);
}

/* Notification of change in stream directory.
 */
static int kvswatch_cb (const char *dir_key, flux_kvsdir_t *dir,
                        void *arg, int errnum)
{
    kz_t *kz = arg;

    if (errnum == ENOENT)
        kz->last_dir_size = 0;
    else if (errnum == 0)
        kz->last_dir_size = flux_kvsdir_get_size (dir);
    else
        goto error;
    if (lookup_next (kz) < 0)
        goto error;
    return 0;
error:
    errnum_save (kz, errnum);
    if (kz->ready_cb)
        kz->ready_cb (kz, kz->ready_arg);
    return 0;
}

/* Send request to lookup the next block (kz->seq).
 * If kz->last_dir_size blocks have already been consumed,
 * install a KVS watch to notify us when more blocks are available
 * (unless already at EOF).
 */
static int lookup_next (kz_t *kz)
{
    if (kz->lookup_f != NULL)
        return 0;

    if (kz->seq < kz->last_dir_size) {
        const char *key = format_key (kz, kz->seq);
        if (!(kz->lookup_f = flux_kvs_lookup (kz->h, 0, key)))
            return -1;
        if (flux_future_then (kz->lookup_f, -1., lookup_continuation, kz) < 0) {
            flux_future_destroy (kz->lookup_f);
            kz->lookup_f = NULL;
            return -1;
        }
    }
    /* For NOFOLLOW, simulate EOF once all known blocks consumed */
    else if (kz->flags & KZ_FLAGS_NOFOLLOW) {
        kz->eof = true;
         /*
          *  Calling unwatch on the kz here may not be necessary as NOFOLLOW
          *   implies we never needed to set the watch below. Howver, it is
          *   harmless to call kz_unwatch() on a kz object without a watch
          *   installed, and there may be a rare or future case where a watch
          *   is somehow being used with NOFOLLOW, so it is safer to cover
          *   this case here.
          */
        kz_unwatch (kz);
        /*
         *  Now call users ready_cb to process our simulated EOF
         */
        if (kz->ready_cb)
            kz->ready_cb (kz, kz->ready_arg);
    }
    /* EOF not yet reached, but all known blocks have been consumed.
     * Time to KVS watch the stream directory for more entries.
     */
    else if (!kz->eof) {
        if (!kz->watching) {
            kz->watching = true; // N.B. careful to avoid infinite loop here!
            const char *key = clear_key (kz);
            if (flux_kvs_watch_dir (kz->h, kvswatch_cb, kz, "%s", key) < 0)
                return -1;
        }
    }
    return 0;
}

/* Handle response containing kz->last_dir_size.
 * Initiate next request (or install a KVS watcher) in lookup_next().
 */
static void lookup_dir_continuation (flux_future_t *f, void *arg)
{
    kz_t *kz = arg;
    const flux_kvsdir_t *dir;

    assert (f == kz->lookup_f);

    if (flux_kvs_lookup_get_dir (f, &dir) < 0) {
        if (errno == ENOENT)
            kz->last_dir_size = 0;
        else
            goto error;
    }
    else
        kz->last_dir_size = flux_kvsdir_get_size (dir);

    flux_future_destroy (kz->lookup_f);
    kz->lookup_f = NULL;

    if (lookup_next (kz) < 0)
        goto error;
    return;
error:
    errnum_save (kz, errno);
    if (kz->ready_cb)
        kz->ready_cb (kz, kz->ready_arg);
}

/* Send request to lookup kz->last_dir_size.
 */
static int lookup_dir (kz_t *kz)
{
    if (kz->lookup_f != NULL)
        return 0;

    const char *key = clear_key (kz);
    if (!(kz->lookup_f = flux_kvs_lookup (kz->h, FLUX_KVS_READDIR, key)))
        return -1;
    if (flux_future_then (kz->lookup_f, -1.,
                          lookup_dir_continuation, kz) < 0) {
        flux_future_destroy (kz->lookup_f);
        kz->lookup_f = NULL;
        return -1;
    }
    return 0;
}

int kz_set_ready_cb (kz_t *kz, kz_ready_f ready_cb, void *arg)
{
    if (!kz || !(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        return -1;
    }
    kz->ready_cb = ready_cb;
    kz->ready_arg = arg;

    /* Callback registration.
     * Begin looking up stream directory, continued in lookup_continuation().
     */
    if (kz->ready_cb != NULL) {
        if (lookup_dir (kz) < 0)
            return -1;
    }
    /* Callback de-registration.
     * Unwire KVS watcher, if any.
     */
    else if (kz->ready_cb == NULL && kz->watching) {
        const char *key = clear_key (kz);
        if (flux_kvs_unwatch (kz->h, key) < 0)
            return -1;
        kz->watching = false;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
