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
 * If KZ_FLAGS_TRUNC, any existing contents are removed.
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
#include <json.h>
#include <flux/core.h>

#include "kz.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/modules/libzio/zio.h"

struct kz_struct {
    int flags;
    char *name;
    char *stream;
    flux_t h;
    int seq;
    kvsdir_t dir;
    kz_ready_f ready_cb;
    void *ready_arg;
    bool eof;
    int nprocs;
    char *grpname;
    int fencecount;
};

static void kz_destroy (kz_t kz)
{
    if (kz->name)
        free (kz->name);
    if (kz->dir)
        kvsdir_destroy (kz->dir);
    if (kz->grpname)
        free (kz->grpname);
    free (kz);
}

kz_t kz_open (flux_t h, const char *name, int flags)
{
    kz_t kz = xzmalloc (sizeof (*kz));

    kz->flags = flags;
    kz->name = xstrdup (name);
    if ((kz->stream = strchr (kz->name, '.')))
        kz->stream++;
    else
        kz->stream = kz->name;
    kz->h = h;

    if ((flags & KZ_FLAGS_WRITE)) {
        if (!(flags & KZ_FLAGS_TRUNC)) {
            if (kvs_get_dir (h, NULL, "%s", name) == 0) {
                errno = EEXIST;
                goto error;
            }
        }
        if (kvs_mkdir (h, name) < 0)
            goto error;
        if (!(flags & KZ_FLAGS_NOCOMMIT_OPEN)) {
            if (kvs_commit (h) < 0)
                goto error;
        }
    } else if ((flags & KZ_FLAGS_READ)) {
        if (!(flags & KZ_FLAGS_NOEXIST)) {
            if (kvs_get_dir (h, &kz->dir, "%s", name) < 0)
                goto error;
        }
    }
    return kz;
error:
    kz_destroy (kz);
    return NULL;
}

static int kz_fence (kz_t kz)
{
    char *name;
    int rc;
    if (asprintf (&name, "%s.%d", kz->grpname, kz->fencecount++) < 0)
        oom ();
    rc = kvs_fence (kz->h, name, kz->nprocs);
    free (name);
    return rc;
}

kz_t kz_gopen (flux_t h, const char *grpname, int nprocs,
               const char *name, int flags)
{
    kz_t kz;

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

static int putnext (kz_t kz, json_object *val)
{
    char *key = NULL;
    int rc = -1;

    if (!(kz->flags & KZ_FLAGS_WRITE)) {
        errno = EINVAL;
        goto done;
    }
    if (asprintf (&key, "%s.%.6d", kz->name, kz->seq++) < 0)
        oom ();
    if (kvs_put (kz->h, key, val) < 0)
        goto done;
    if (!(kz->flags & KZ_FLAGS_NOCOMMIT_PUT)) {
        if (kvs_commit (kz->h) < 0)
            goto done;
    }
    rc = 0;
done:
    if (key)
        free (key);
    return rc;
}

int kz_put_json (kz_t kz, json_object *val)
{
    if (!(kz->flags & KZ_FLAGS_RAW)) {
        errno = EINVAL;
        return -1;
    }
    return putnext (kz, val);
}

int kz_put (kz_t kz, char *data, int len)
{
    json_object *val = NULL;
    int rc = -1;

    if (len == 0 || data == NULL || (kz->flags & KZ_FLAGS_RAW)) {
        errno = EINVAL;
        goto done;
    }
    if (!(val = zio_json_encode (data, len, false))) {
        errno = EPROTO;
        goto done;
    }
    if (putnext (kz, val) < 0)
        goto done;
    rc = len;
done:
    if (val)
        json_object_put (val);
    return rc;
}

static json_object *getnext (kz_t kz)
{
    json_object *val = NULL;
    char *key = NULL;

    if (!(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        goto done;
    }
    if (asprintf (&key, "%s.%.6d", kz->name, kz->seq) < 0)
        oom ();
    if (kvs_get (kz->h, key, &val) < 0) {
        if (errno == ENOENT)
            errno = EAGAIN;
        goto done;
    } else
        kz->seq++;
done:
    if (key)
        free (key);
    return val;
}

static json_object *getnext_blocking (kz_t kz)
{
    json_object *val;

    while (!(val = getnext (kz))) {
        if (errno != EAGAIN)
            break;
        if (kvs_watch_once_dir (kz->h, &kz->dir, "%s", kz->name) < 0) {
            if (errno != ENOENT)
                break;
            if (kz->dir) {
                kvsdir_destroy (kz->dir);
                kz->dir = NULL;
            }
        }
    }
    return val;
}

json_object *kz_get_json (kz_t kz)
{
    json_object *val = NULL;

    if (!(kz->flags & KZ_FLAGS_RAW)) {
        errno = EINVAL;
        goto done;
    }
    if ((kz->flags & KZ_FLAGS_NONBLOCK))
        val = getnext (kz);
    else
        val = getnext_blocking (kz);
done:
    return val;
}

int kz_get (kz_t kz, char **datap)
{
    json_object *val = NULL;
    char *data;
    int len = -1;

    if (!datap || (kz->flags & KZ_FLAGS_RAW)) {
        errno = EINVAL;
        goto done;
    }
    if (kz->eof)
        return 0;
    if ((kz->flags & KZ_FLAGS_NONBLOCK))
        val = getnext (kz);
    else
        val = getnext_blocking (kz);
    if (!val)
        goto done;
    if ((len = zio_json_decode (val, (void **) &data, &kz->eof)) < 0) {
        errno = EPROTO;
        goto done;
    }
    *datap = data;
done:
    return len;
}

int kz_flush (kz_t kz)
{
    int rc = 0;
    if ((kz->flags & KZ_FLAGS_WRITE))
        rc = kvs_commit (kz->h);
    return rc;
}

int kz_close (kz_t kz)
{
    int rc = -1;
    json_object *val = NULL;
    char *key = NULL;

    if ((kz->flags & KZ_FLAGS_WRITE)) {
        if (!(kz->flags & KZ_FLAGS_RAW)) {
            if (asprintf (&key, "%s.%.6d", kz->name, kz->seq++) < 0)
                oom ();
            if (!(val = zio_json_encode (NULL, 0, true))) { /* EOF */
                errno = EPROTO;
                goto done;
            }
            if (kvs_put (kz->h, key, val) < 0)
                goto done;
        }
        if (!(kz->flags & KZ_FLAGS_NOCOMMIT_CLOSE)) {
            if (kvs_commit (kz->h) < 0)
                goto done;
        }
        if (kz->nprocs > 0 && kz->grpname) {
            if (kz_fence (kz) < 0)
                goto done;
        }
    }
    rc = 0;
done:
    if (val)
        json_object_put (val);
    if (key)
        free (key);
    kz_destroy (kz);
    return rc;
}

int kvswatch_cb (const char *key, kvsdir_t dir, void *arg, int errnum)
{
    kz_t kz = arg;

    if (errnum != 0 && errnum != ENOENT)
        return -1;
    else if (errnum == 0 && kz->ready_cb)
        kz->ready_cb (kz, kz->ready_arg);
    return 0;
}

int kz_set_ready_cb (kz_t kz, kz_ready_f ready_cb, void *arg)
{
    if (!(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        return -1;
    }
    kz->ready_cb = ready_cb;
    kz->ready_arg = arg;
    if (kvs_watch_dir (kz->h, kvswatch_cb, kz, "%s", kz->name) < 0)
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
