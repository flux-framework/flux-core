/* kz.c - KVS streams */

/* We use a kvs directory to represent a character stream.
 * Blocks are written as sequenced keys (monotonic int) in the directory.
 * Each block is represented as a zio json frame.
 *
 * kz_get (only valid for kz_open KZ_FLAGS_READ):
 * We try to kvs_get '000000' from the stream.  If ESRCH, we either block
 * until that key appears, or if KZ_FLAGS_NONBLOCK, return -1, errno = EAGAIN.
 * Once we have the value, its data is extracted and returned.
 * If the EOF flag was set, the next read will return 0, otherwise the next
 * read repeats the above for '000001' and so on.
 *
 * kz_put (only valid for kz_open KZ_FLAGS_WRITE):
 * If KVS_FLAGS_APPEND, we seek to next available key and begin writing there.
 * If the EOF flag is encountered before that, return -1, errno = EINVAL.
 * If not appending, any existing contents are removed and writing begins
 * at '000000'.  Each kz_put returns either -1 or the number of bytes requested
 * to be written (there are no short writes).
 *
 * kz_flush (only valid for kz_open KZ_FLAGS_WRITE):
 * Issue a kvs_commit().
 *
 * kz_close
 * Issues a kz_flush if KZ_FLAGS_WRITE and destroys the handle.
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <sys/wait.h>
#include <termios.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "zio.h"
#include "kz.h"

struct kz_struct {
    int flags;
    char *name;
    char *stream;
    flux_t h;
    int seq;
    kvsdir_t dir;
    kz_ready_f ready_cb;
    void *ready_arg;
};

kz_t kz_open (flux_t h, const char *name, int flags)
{
    kz_t kz = xzmalloc (sizeof (*kz));

    if ((flags & KZ_FLAGS_APPEND)) {
        errno = EINVAL; /* not yet supported */
        goto error;
    }

    kz->flags = flags;
    kz->name = xstrdup (name);
    if ((kz->stream = strchr (kz->name, '.')))
        kz->stream++;
    else
        kz->stream = kz->name;
    kz->h = h;

    if ((flags & KZ_FLAGS_WRITE)) {
        if ((flags & KZ_FLAGS_TRUNC)) {
            if (kvs_unlink (h, name) < 0)
                goto error;
        } else {
            if (kvs_get_dir (h, &kz->dir, "%s", name) == 0) {
                errno = EEXIST;
                goto error;
            }
        }
    }
    return kz;
error:
    if (kz->name)
        free (kz->name);
    if (kz->dir)
        kvsdir_destroy (kz->dir);
    free (kz);
    return NULL;
}

int kz_put (kz_t kz, char *data, int len)
{
    json_object *val = NULL;
    char *key = NULL;
    int rc = -1;

    if (len == 0 || data == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (!(val = zio_json_encode (data, len, false, kz->stream))) {
        errno = EPROTO;
        goto done;
    }
    if (asprintf (&key, "%s.%.6d", kz->name, kz->seq++) < 0)
        oom ();
    if (kvs_put (kz->h, key, val) < 0)
        goto done;
    /* FIXME: use flags to regulate this? */
    if (kvs_commit (kz->h) < 0)
        goto done;
    rc = len;
done:
    if (key)
        free (key);
    if (val)
        json_object_put (val);
    return rc;
}

static json_object *getnext (kz_t kz)
{
    json_object *val = NULL;
    char *key;
    
    if (asprintf (&key, "%s.%.6d", kz->name, kz->seq) < 0)
        oom ();
    if (kvs_get (kz->h, key, &val) < 0) {
        if (errno == ENOENT)
            errno = EAGAIN;
    } else
        kz->seq++;
    free (key);
    return val;    
}

static json_object *getnext_blocking (kz_t kz)
{
    json_object *val = NULL;
    char *key;
    bool refresh = false;

    if (asprintf (&key, "%.6d", kz->seq) < 0)
        oom ();
    do {
        while (!kz->dir || refresh) {
            if (kvs_watch_once_dir (kz->h, &kz->dir, "%s", kz->name) < 0) {
                if (errno != ENOENT)
                    goto done;
            }
        }
        if (kvsdir_get (kz->dir, key, &val) < 0) {
            if (errno != ENOENT)
                goto done;
            refresh = true;
        }
    } while (val == NULL);
    kz->seq++;
done:
    if (key)
        free (key);
    return val;
}

int kz_get (kz_t kz, char **datap)
{
    json_object *val = NULL;
    char *stream = NULL;
    bool eof = false;
    char *data;
    int len = -1;

    if (!datap || !(kz->flags & KZ_FLAGS_READ)) {
        errno = EINVAL;
        goto done;
    }
    if ((kz->flags & KZ_FLAGS_NONBLOCK))
        val = getnext (kz);
    else
        val = getnext_blocking (kz);
    if (!val)
        goto done;
    if ((len = zio_json_decode (val, &data, &eof, &stream)) < 0) {
        errno = EPROTO;
        goto done;
    }
    if ((len > 0 && eof) || strcmp (stream, kz->stream) != 0) {
        errno = EPROTO;
        goto done;
    }
    *datap = data;
done:
    if (stream)
        free (stream);
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
        if (asprintf (&key, "%s.%.6d", kz->name, kz->seq++) < 0)
            oom ();
        if (!(val = zio_json_encode (NULL, 0, true, kz->stream))) { /* EOF */
            errno = EPROTO;
            goto done;
        }
        if (kvs_put (kz->h, key, val) < 0)
            goto done;
        if (kvs_commit (kz->h) < 0)
            goto done;
    }
done:
    if (val)
        json_object_put (val);
    if (key)
        free (key);
    free (kz->name);
    if (kz->dir)
        kvsdir_destroy (kz->dir);
    free (kz);
    rc = 0;

    return rc;
}

void kvswatch_cb (const char *key, kvsdir_t dir, void *arg, int errnum)
{
    kz_t kz = arg;

    if (errnum != 0 && errnum != ENOENT)
        flux_reactor_stop (kz->h);
    else if (errnum == 0 && kz->ready_cb)
        kz->ready_cb (kz, kz->ready_arg);
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
