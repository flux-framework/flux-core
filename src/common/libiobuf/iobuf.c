/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <sys/types.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>

#include <czmq.h>
#include <sodium.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"

#include "iobuf.h"

struct ioinfo {
    char *stream;
    int rank;
    int data_len;
    bool eof;
    struct iodata *head;
    struct iodata *tail;
};

struct iodata {
    struct ioinfo *ioinfo;
    char *data;
    int data_len;
    struct iodata *next;
};

struct iobuf {
    flux_t *h;
    flux_reactor_t *r;
    char *name;
    int max_count;
    int flags;

    flux_watcher_t *eof_count_cb_prep_w;
    flux_watcher_t *eof_count_cb_idle_w;
    flux_watcher_t *eof_count_cb_check_w;
    int eof_count_cb_count;
    iobuf_f eof_count_cb;
    void *eof_count_cb_arg;
    bool eof_count_cb_called;

    struct iobuf_data iobuf_data; /* used in iterators */
    zlist_t *data;
    zhash_t *streamranks;
    int eof_count;

    flux_msg_handler_t *create_mh;
    flux_msg_handler_t *write_mh;
    flux_msg_handler_t *eof_mh;
    flux_msg_handler_t *read_mh;
};

static void iobuf_log_error (iobuf_t *iob, const char *fmt, ...)
{
    if (iob->flags & IOBUF_FLAG_LOG_ERRORS) {
        va_list va;
        va_start (va, fmt);
        flux_log_verror (iob->h, fmt, va);
        va_end (va);
    }
}

static char *topic_str (const char *name, const char *suffix)
{
    char *topic = NULL;
    if (asprintf (&topic,
                  "%s.%s",
                  name,
                  suffix) < 0)
        return NULL;
    return topic;
}

static char *streamrank_key (const char *stream, int rank)
{
    char *key;
    if (asprintf (&key,
                  "%s.%d",
                  stream,
                  rank) < 0)
        return NULL;
    return key;
}

static char *bin2base64 (const char *bin_data, int bin_len)
{
    char *base64_data;
    int base64_len;

    base64_len = sodium_base64_encoded_len (bin_len, sodium_base64_VARIANT_ORIGINAL);

    if (!(base64_data = calloc (1, base64_len)))
        return NULL;

    sodium_bin2base64 (base64_data, base64_len,
                       (unsigned char *)bin_data, bin_len,
                       sodium_base64_VARIANT_ORIGINAL);

    return base64_data;
}

static char *base642bin (const char *base64_data, size_t *bin_len)
{
    size_t base64_len;
    char *bin_data = NULL;

    base64_len = strlen (base64_data);
    (*bin_len) = BASE64_DECODE_SIZE (base64_len);

    if (!(bin_data = calloc (1, (*bin_len))))
        return NULL;

    if (sodium_base642bin ((unsigned char *)bin_data, (*bin_len),
                           base64_data, base64_len,
                           NULL, bin_len, NULL,
                           sodium_base64_VARIANT_ORIGINAL) < 0) {
        free (bin_data);
        return NULL;
    }

    return bin_data;
}

static void create_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    iobuf_t *iob = arg;
    const char *stream;
    int rank;

    if (flux_request_unpack (msg, NULL, "{s:s s:i}",
                             "stream", &stream,
                             "rank", &rank) < 0)
        goto error;

    if (iobuf_create (iob, stream, rank) < 0)
        goto error;

    if (flux_respond (h, msg, NULL) < 0) {
        iobuf_log_error (iob, "flux_respond");
        goto error;
    }

    return;

error:
    if (flux_respond_error (iob->h, msg, errno, NULL) < 0)
        iobuf_log_error (iob, "flux_respond_error");
}

static void write_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    iobuf_t *iob = arg;
    const char *stream;
    const char *data;
    int rank;
    size_t bin_len;
    char *bin_data = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s s:i s:s}",
                             "stream", &stream,
                             "rank", &rank,
                             "data", &data) < 0)
        goto error;

    if (!(bin_data = base642bin (data, &bin_len)))
        goto error;

    if (iobuf_write (iob, stream, rank, bin_data, bin_len) < 0) {
        iobuf_log_error (iob, "iobuf_write");
        goto error;
    }

    if (flux_respond (h, msg, NULL) < 0) {
        iobuf_log_error (iob, "flux_respond");
        goto error;
    }

    free (bin_data);
    return;

error:
    if (flux_respond_error (iob->h, msg, errno, NULL) < 0)
        iobuf_log_error (iob, "flux_respond_error");
    free (bin_data);
}

static void eof_cb (flux_t *h, flux_msg_handler_t *mh,
                    const flux_msg_t *msg, void *arg)
{
    iobuf_t *iob = arg;
    const char *stream;
    int rank;

    if (flux_request_unpack (msg, NULL, "{s:s s:i}",
                             "stream", &stream,
                             "rank", &rank) < 0)
        goto error;

    if (iobuf_eof (iob, stream, rank) < 0)
        goto error;

    if (flux_respond (h, msg, NULL) < 0) {
        iobuf_log_error (iob, "flux_respond");
        goto error;
    }

    return;

error:
    if (flux_respond_error (iob->h, msg, errno, NULL) < 0)
        iobuf_log_error (iob, "flux_respond_error");
}

static void read_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    iobuf_t *iob = arg;
    char *data = NULL;
    const char *stream;
    char *base64data = NULL;
    int rank, data_len;

    if (flux_request_unpack (msg, NULL, "{s:s s:i}",
                             "stream", &stream,
                             "rank", &rank) < 0)
        goto error;

    if (iobuf_read (iob, stream, rank, &data, &data_len) < 0) {
        iobuf_log_error (iob, "iobuf_read");
        goto error;
    }

    if (!(base64data = bin2base64 (data, data_len)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:s}", "data", base64data) < 0) {
        iobuf_log_error (iob, "flux_respond_pack");
        goto error;
    }

    free (data);
    free (base64data);
    return;

error:
    if (flux_respond_error (iob->h, msg, errno, NULL) < 0)
        iobuf_log_error (iob, "flux_respond_error");
    free (data);
    free (base64data);
}

static flux_msg_handler_t *setup_handler (iobuf_t *iob, flux_msg_handler_f cb,
                                          const char *suffix)
{
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *mh = NULL;

    if (!(match.topic_glob = topic_str (iob->name, suffix)))
        goto cleanup;

    if (!(mh = flux_msg_handler_create (iob->h,
                                        match,
                                        cb,
                                        iob)))
        goto cleanup;

    flux_msg_handler_allow_rolemask (mh, FLUX_ROLE_OWNER);
    flux_msg_handler_start (mh);

    free (match.topic_glob);
    return mh;

cleanup:
    flux_msg_handler_destroy (mh);
    free (match.topic_glob);
    return NULL;
}

static int setup_cbs (iobuf_t *iob)
{
    if (!(iob->create_mh = setup_handler (iob, create_cb, "create")))
        return -1;

    if (!(iob->write_mh = setup_handler (iob, write_cb, "write")))
        return -1;

    if (!(iob->eof_mh = setup_handler (iob, eof_cb, "eof")))
        return -1;

    if (!(iob->read_mh = setup_handler (iob, read_cb, "read")))
        return -1;

    return 0;
}

iobuf_t *iobuf_server_create (flux_t *h,
                              const char *name,
                              int max_count,
                              int flags)
{
    iobuf_t *iob = NULL;
    int save_errno;
    int valid_flags = IOBUF_FLAG_LOG_ERRORS;

    if (!h
        || !name
        || max_count < 0
        || (flags & ~valid_flags)) {
        errno = EINVAL;
        return NULL;
    }

    iob = calloc (1, sizeof (*iob));
    if (!iob) {
        errno = ENOMEM;
        goto cleanup;
    }
    iob->h = h;
    if (!(iob->r = flux_get_reactor (h)))
        goto cleanup;
    iob->max_count = max_count;
    iob->flags = flags;

    if (!(iob->name = strdup (name))) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (!(iob->data = zlist_new ()))
        goto cleanup;
    if (!(iob->streamranks = zhash_new ()))
        goto cleanup;

    if (setup_cbs (iob) < 0)
        goto cleanup;

    return iob;

cleanup:
    save_errno = errno;
    iobuf_server_destroy (iob);
    errno = save_errno;
    return NULL;
}

void iobuf_server_destroy (iobuf_t *iob)
{
    if (iob) {
        flux_watcher_destroy (iob->eof_count_cb_prep_w);
        flux_watcher_destroy (iob->eof_count_cb_idle_w);
        flux_watcher_destroy (iob->eof_count_cb_check_w);
        zlist_destroy (&iob->data);
        zhash_destroy (&iob->streamranks);
        flux_msg_handler_destroy (iob->create_mh);
        flux_msg_handler_destroy (iob->write_mh);
        flux_msg_handler_destroy (iob->eof_mh);
        flux_msg_handler_destroy (iob->read_mh);
        free (iob);
    }
}

static void eof_count_cb_prep_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    iobuf_t *iob = arg;
    flux_watcher_start (iob->eof_count_cb_idle_w);
}

static void eof_count_cb_check_cb (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    iobuf_t *iob = arg;

    flux_watcher_stop (iob->eof_count_cb_prep_w);
    flux_watcher_stop (iob->eof_count_cb_idle_w);
    flux_watcher_stop (iob->eof_count_cb_check_w);

    if (iob->eof_count_cb
        && !iob->eof_count_cb_called)
        iob->eof_count_cb (iob, iob->eof_count_cb_arg);

    iob->eof_count_cb_called = true;
}

int iobuf_set_eof_count_cb (iobuf_t *iob,
                            int eof_count,
                            iobuf_f eof_count_cb,
                            void *arg)
{
    if (!iob || eof_count <= 0 || !eof_count_cb) {
        errno = EINVAL;
        return -1;
    }
    iob->eof_count_cb_prep_w = flux_prepare_watcher_create (iob->r,
                                                            eof_count_cb_prep_cb,
                                                            iob);
    if (!iob->eof_count_cb_prep_w) {
        iobuf_log_error (iob, "flux_prepare_watcher_create");
        return -1;
    }
    iob->eof_count_cb_idle_w = flux_idle_watcher_create (iob->r,
                                                         NULL,
                                                         iob);
    if (!iob->eof_count_cb_idle_w) {
        iobuf_log_error (iob, "flux_idle_watcher_create");
        return -1;
    }
    iob->eof_count_cb_check_w = flux_check_watcher_create (iob->r,
                                                           eof_count_cb_check_cb,
                                                           iob);
    if (!iob->eof_count_cb_check_w) {
        iobuf_log_error (iob, "flux_check_watcher_create");
        return -1;
    }
    iob->eof_count_cb_count = eof_count;
    iob->eof_count_cb = eof_count_cb;
    iob->eof_count_cb_arg = arg;
    return 0;
}

static void ioinfo_destroy (void *data)
{
    if (data) {
        struct ioinfo *ioinfo = data;
        free (ioinfo->stream);
        free (ioinfo);
    }
}

static struct ioinfo *ioinfo_create (const char *stream, int rank)
{
    struct ioinfo *ioinfo = NULL;
    int save_errno;

    if (!(ioinfo = calloc (1, sizeof (*ioinfo)))) {
        errno = ENOMEM;
        return NULL;
    }
    if (!(ioinfo->stream = strdup (stream))) {
        errno = ENOMEM;
        goto cleanup;
    }
    ioinfo->rank = rank;
    ioinfo->eof = false;
    ioinfo->head = NULL;
    ioinfo->tail = NULL;
    return ioinfo;

cleanup:
    save_errno = errno;
    ioinfo_destroy (ioinfo);
    errno = save_errno;
    return NULL;
}

static void iodata_destroy (void *data)
{
    if (data) {
        struct iodata *iodata = data;
        free (iodata->data);
        free (iodata);
    }
}

static struct iodata *iodata_create (struct ioinfo *ioinfo,
                                     const char *data,
                                     int data_len)
{
    struct iodata *iodata = NULL;
    int save_errno;

    if (!(iodata = calloc (1, sizeof (*iodata)))) {
        errno = ENOMEM;
        return NULL;
    }
    iodata->ioinfo = ioinfo;
    if (!(iodata->data = malloc (data_len))) {
        errno = ENOMEM;
        goto cleanup;
    }
    memcpy (iodata->data, data, data_len);
    iodata->data_len = data_len;
    return iodata;

cleanup:
    save_errno = errno;
    iodata_destroy (iodata);
    errno = save_errno;
    return NULL;
}

static struct ioinfo *lookup_buffer (iobuf_t *iob,
                                     const char *stream,
                                     int rank)
{
    struct ioinfo *ioinfo;
    char *key = NULL;

    if (!(key = streamrank_key (stream, rank)))
        return NULL;
    if (!(ioinfo = zhash_lookup (iob->streamranks, key)))
        errno = ENOENT;
    free (key);
    return ioinfo;
}

static struct ioinfo *create_buffer (iobuf_t *iob,
                                     const char *stream,
                                     int rank)
{
    struct ioinfo *ioinfo = NULL;
    char *key = NULL;
    int save_errno, ret;

    if (iob->max_count
        && zhash_size (iob->streamranks) >= iob->max_count) {
        /* XXX: ok errno? */
        errno = ENFILE;
        goto cleanup;
    }

    if (!(key = streamrank_key (stream, rank)))
        goto cleanup;

    if (!(ioinfo = ioinfo_create (stream, rank)))
        goto cleanup;

    /* caller should check for existence first */
    ret = zhash_insert (iob->streamranks, key, ioinfo);
    assert (ret == 0);
    zhash_freefn (iob->streamranks, key, ioinfo_destroy);

    free (key);
    return ioinfo;

cleanup:
    save_errno = errno;
    free (key);
    ioinfo_destroy (ioinfo);
    errno = save_errno;
    return NULL;
}

int iobuf_create (iobuf_t *iob,
                  const char *stream,
                  int data_rank)
{
    struct ioinfo *ioinfo;

    if (!iob || !stream) {
        errno = EINVAL;
        return -1;
    }

    if (!(ioinfo = lookup_buffer (iob, stream, data_rank))) {
        if (!(ioinfo = create_buffer (iob, stream, data_rank)))
            return -1;
    }
    else {
        errno = EEXIST;
        return -1;
    }

    return 0;
}

int iobuf_write (iobuf_t *iob,
                 const char *stream,
                 int data_rank,
                 const char *data,
                 int data_len)
{
    struct ioinfo *ioinfo;
    struct iodata *iodata = NULL;
    int save_errno;

    if (!iob || !stream || !data || data_len < 0) {
        errno = EINVAL;
        return -1;
    }

    if (!(ioinfo = lookup_buffer (iob, stream, data_rank))) {
        if (!(ioinfo = create_buffer (iob, stream, data_rank)))
            return -1;
    }

    if (ioinfo->eof) {
        errno = EROFS;
        return -1;
    }

    if (!(iodata = iodata_create (ioinfo, data, data_len)))
        return -1;

    if (zlist_append (iob->data, iodata) < 0)
        goto cleanup;
    zlist_freefn (iob->data, iodata, iodata_destroy, true);

    if (!ioinfo->head) {
        ioinfo->head = iodata;
        ioinfo->tail = iodata;
    }
    else {
        ioinfo->tail->next = iodata;
        ioinfo->tail = iodata;
    }

    ioinfo->data_len += data_len;
    return 0;

cleanup:
    save_errno = errno;
    iodata_destroy (iodata);
    errno = save_errno;
    return -1;
}

int iobuf_eof (iobuf_t *iob,
               const char *stream,
               int data_rank)
{
    struct ioinfo *ioinfo;

    if (!iob || !stream) {
        errno = EINVAL;
        return -1;
    }

    if (!(ioinfo = lookup_buffer (iob, stream, data_rank))) {
        if (!(ioinfo = create_buffer (iob, stream, data_rank)))
            return -1;
    }

    if (!ioinfo->eof) {
        ioinfo->eof = true;
        iob->eof_count++;
    }

    if (iob->eof_count_cb_count
        && iob->eof_count >= iob->eof_count_cb_count
        && !iob->eof_count_cb_called) {
        flux_watcher_start (iob->eof_count_cb_prep_w);
        flux_watcher_start (iob->eof_count_cb_check_w);
    }

    return 0;
}

int iobuf_read (iobuf_t *iob,
                const char *stream,
                int data_rank,
                char **data,
                int *data_len)
{
    struct ioinfo *ioinfo;
    char *bin_data = NULL;
    int bin_len;

    if (!iob || !stream) {
        errno = EINVAL;
        return -1;
    }

    if (!(ioinfo = lookup_buffer (iob, stream, data_rank))) {
        errno = ENOENT;
        return -1;
    }

    bin_len = ioinfo->data_len;
    if (data) {
        if (bin_len) {
            struct iodata *iodata;
            int len = 0;
            if (!(bin_data = malloc (ioinfo->data_len))) {
                errno = ENOMEM;
                return -1;
            }
            iodata = ioinfo->head;
            while (iodata) {
                memcpy (bin_data + len, iodata->data, iodata->data_len);
                len += iodata->data_len;
                iodata = iodata->next;
            }
        }
        (*data) = bin_data;
    }

    if (data_len)
        (*data_len) = bin_len;

    return 0;
}

static void set_iobuf_data (iobuf_t *iob, struct iodata *iodata)
{
    iob->iobuf_data.stream = iodata->ioinfo->stream;
    iob->iobuf_data.rank = iodata->ioinfo->rank;
    iob->iobuf_data.data = iodata->data;
    iob->iobuf_data.data_len = iodata->data_len;
}

const struct iobuf_data *iobuf_iter_first (iobuf_t *iob)
{
    struct iodata *iodata;

    if (!iob) {
        errno = EINVAL;
        return NULL;
    }

    if ((iodata = zlist_first (iob->data))) {
        set_iobuf_data (iob, iodata);
        return &(iob->iobuf_data);
    }
    return NULL;
}

const struct iobuf_data *iobuf_iter_next (iobuf_t *iob)
{
    struct iodata *iodata;

    if (!iob) {
        errno = EINVAL;
        return NULL;
    }

    if ((iodata = zlist_next (iob->data))) {
        set_iobuf_data (iob, iodata);
        return &(iob->iobuf_data);
    }
    return NULL;
}

int iobuf_count (iobuf_t *iob)
{
    if (!iob) {
        errno = EINVAL;
        return -1;
    }

    return zhash_size (iob->streamranks);
}

int iobuf_eof_count (iobuf_t *iob)
{
    if (!iob) {
        errno = EINVAL;
        return -1;
    }

    return iob->eof_count;
}

flux_future_t *iobuf_rpc_create (flux_t *h,
                                 const char *name,
                                 int rpc_rank,
                                 const char *stream,
                                 int data_rank)
{
    flux_future_t *f = NULL;
    char *topic = NULL;

    if (!h || !name || !stream) {
        errno = EINVAL;
        return NULL;
    }

    if (!(topic = topic_str (name, "create")))
        goto cleanup;

    f = flux_rpc_pack (h, topic, rpc_rank, 0,
                       "{s:s s:i}",
                       "stream", stream,
                       "rank", data_rank);
cleanup:
    free (topic);
    return f;
}

flux_future_t *iobuf_rpc_write (flux_t *h,
                                const char *name,
                                int rpc_rank,
                                const char *stream,
                                int data_rank,
                                const char *data,
                                int data_len)
{
    flux_future_t *f = NULL;
    char *topic = NULL;
    char *base64_data = NULL;

    if (!h || !name || !stream || !data || data_len < 0) {
        errno = EINVAL;
        return NULL;
    }

    if (!(topic = topic_str (name, "write")))
        goto cleanup;

    if (!(base64_data = bin2base64 (data, data_len)))
        goto cleanup;

    f = flux_rpc_pack (h, topic, rpc_rank, 0,
                       "{s:s s:i s:s}",
                       "stream", stream,
                       "rank", data_rank,
                       "data", base64_data);
cleanup:
    free (topic);
    free (base64_data);
    return f;
}

flux_future_t *iobuf_rpc_eof (flux_t *h,
                              const char *name,
                              int rpc_rank,
                              const char *stream,
                              int data_rank)
{
    flux_future_t *f = NULL;
    char *topic = NULL;

    if (!h || !name || !stream) {
        errno = EINVAL;
        return NULL;
    }

    if (!(topic = topic_str (name, "eof")))
        goto cleanup;

    f = flux_rpc_pack (h, topic, rpc_rank, 0,
                       "{s:s s:i}",
                       "stream", stream,
                       "rank", data_rank);
cleanup:
    free (topic);
    return f;
}

flux_future_t *iobuf_rpc_read (flux_t *h,
                               const char *name,
                               int rpc_rank,
                               const char *stream,
                               int data_rank)
{
    flux_future_t *f = NULL;
    char *topic = NULL;

    if (!h || !name || !stream) {
        errno = EINVAL;
        return NULL;
    }

    if (!(topic = topic_str (name, "read")))
        return NULL;

    f = flux_rpc_pack (h, topic, rpc_rank, 0,
                       "{s:s s:i}",
                       "stream", stream,
                       "rank", data_rank);
    free (topic);
    return f;
}

int iobuf_rpc_read_get (flux_future_t *f, char **data, int *data_len)
{
    const char *base64_data;
    char *bin_data = NULL;
    size_t bin_len;

    if (!f) {
        errno = EINVAL;
        return -1;
    }

    if (flux_rpc_get_unpack (f, "{s:s}", "data", &base64_data) < 0)
        goto cleanup;

    if (!(bin_data = base642bin (base64_data, &bin_len)))
        goto cleanup;

    if (data)
        (*data) = bin_data;
    else
        free (bin_data);
    if (data_len)
        (*data_len) = bin_len;

    return 0;

cleanup:
    free (bin_data);
    return -1;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
