/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* content-files.c - content addressable storage with files back end
 *
 * This is mainly for demo/experimentation purposes.
 * The "store" is a flat directory with blobrefs as filenames.
 * As such, it is hungry for inodes and may run the file system out of them
 * if used in anger!
 *
 * There are four main operations (RPC handlers):
 *
 * content-backing.load:
 * Given a hash, lookup blob and return it or a "not found" error.
 *
 * content-backing.store:
 * Given a blob, store it and return its hash
 *
 * content-backing.checkpoint-get:
 * Given a string key, lookup string value and return it or a "not found" error.
 *
 * content-backing.checkpoint-put:
 * Given a string key and string value, store it and return.
 * If the key exists, overwrite.
 *
 * The content operations are per RFC 10 and are the main storage behind
 * the Flux KVS.
 *
 * The content-backing.checkpoint operations allow the current KVS
 * root reference to be saved/restored along with the content so it
 * can persist across a Flux instance restart.  Multiple KVS
 * namespaces (each with an independent root) are technically
 * supported, although currently only the main KVS namespace is
 * saved/restored by the KVS module.
 *
 * The main client of this module is the rank 0 content-cache.  The content
 * cache is hierarchical:  each broker resolves missing content-cache entries
 * by asking its TBON parent if it has the missing item.  Rank 0, the TBON
 * root, asks the content backing store module.
 *
 * Once loaded this module can also be exercised directly using
 * flux-content(1) with the --bypass-cache option.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "ccan/str/str.h"

#include "src/common/libcontent/content-util.h"

#include "filedb.h"

struct content_files {
    flux_msg_handler_t **handlers;
    char *dbpath;
    flux_t *h;
    char *hashfun;
    int hash_size;
};

static int file_count_cb (dirwalk_t *d, void *arg)
{
    int *count = arg;

    if (!dirwalk_isdir (d))
        (*count)++;
    return 0;
}

static int get_object_count (const char *path)
{
    int count = 0;
    if (dirwalk (path, 0, file_count_cb, &count) < 0)
        return -1;
    return count;
}

static void stats_get_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct content_files *ctx = arg;
    int count;

    if ((count = get_object_count (ctx->dbpath)) < 0)
        goto error;

    if (flux_respond_pack (h, msg, "{s:i}", "object_count", count) < 0)
        flux_log_error (h, "error responding to stats-get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-get request");
}


/* Handle a content-backing.load request from the rank 0 broker's
 * content-cache service.  The raw request payload is a hash digest.
 * The raw response payload is the blob content.
 * These payloads are specified in RFC 10.
 */
static void load_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct content_files *ctx = arg;
    const void *hash;
    size_t hash_size;
    char blobref[BLOBREF_MAX_STRING_SIZE];
    void *data = NULL;
    size_t size;
    const char *errstr = NULL;

    if (flux_request_decode_raw (msg, NULL, &hash, &hash_size) < 0)
        goto error;
    if (hash_size != ctx->hash_size) {
        errno = EPROTO;
        goto error;
    }
    if (blobref_hashtostr (ctx->hashfun,
                           hash,
                           hash_size,
                           blobref,
                           sizeof (blobref)) < 0)
        goto error;
    if (filedb_get (ctx->dbpath, blobref, &data, &size, &errstr) < 0)
        goto error;
    if (flux_respond_raw (h, msg, data, size) < 0)
        flux_log_error (h, "error responding to load request");
    free (data);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to load request");
    free (data);
}

/* Handle a content-backing.store request from the rank 0 broker's
 * content-cache service.  The raw request payload is the blob content.
 * The raw response payload is hash digest.
 * These payloads are specified in RFC 10.
 */
void store_cb (flux_t *h,
               flux_msg_handler_t *mh,
               const flux_msg_t *msg,
               void *arg)
{
    struct content_files *ctx = arg;
    const void *data;
    size_t size;
    char blobref[BLOBREF_MAX_STRING_SIZE];
    char hash[BLOBREF_MAX_DIGEST_SIZE];
    int hash_size;
    const char *errstr = NULL;

    if (flux_request_decode_raw (msg, NULL, &data, &size) < 0)
        goto error;
    if ((hash_size = blobref_hash_raw (ctx->hashfun,
                                       data,
                                       size,
                                       hash,
                                       sizeof (hash))) < 0)
        goto error;
    if (blobref_hashtostr (ctx->hashfun,
                           hash,
                           hash_size,
                           blobref,
                           sizeof (blobref)) < 0)
        goto error;
    if (filedb_put (ctx->dbpath, blobref, data, size, &errstr) < 0)
        goto error;
    if (flux_respond_raw (h, msg, hash, hash_size) < 0)
        flux_log_error (h, "error responding to store request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to store request");
}

/* Handle a content-backing.checkpoint-get request from the rank 0 kvs module.
 * The KVS stores its last root reference here for restart purposes.
 *
 * N.B. filedb_get() calls read_all() which ensures that the returned buffer
 * is padded with an extra NULL not included in the returned length,
 * so it is safe to use the result as a string argument in flux_respond_pack().
 */
void checkpoint_get_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct content_files *ctx = arg;
    const char *key = KVS_DEFAULT_CHECKPOINT;
    void *data = NULL;
    size_t size;
    json_t *o = NULL;
    const char *errstr = NULL;
    json_error_t error;

    if (flux_request_unpack (msg, NULL, "{s?s}", "key", &key) < 0)
        goto error;
    if (!streq (key, KVS_DEFAULT_CHECKPOINT)) {
        errno = EINVAL;
        goto error;
    }
    if (filedb_get (ctx->dbpath, key, &data, &size, &errstr) < 0)
        goto error;
    /* recovery from version 0 checkpoint blobref not supported */
    if (!(o = json_loadb (data, size, 0, &error))) {
        errstr = error.text;
        errno = EINVAL;
        goto error;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:O}",
                           "value", o) < 0)
        flux_log_error (h, "error responding to checkpoint-get request");
    free (data);
    json_decref (o);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to checkpoint-get request");
    free (data);
    json_decref (o);
}

/* Handle a content-backing.checkpoint-put request from the rank 0 kvs module.
 * The KVS stores its last root reference here for restart purposes.
 */
void checkpoint_put_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct content_files *ctx = arg;
    const char *key = KVS_DEFAULT_CHECKPOINT;
    json_t *o;
    char *value = NULL;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s:o}",
                             "key", &key,
                             "value", &o) < 0)
        goto error;
    if (!streq (key, KVS_DEFAULT_CHECKPOINT)) {
        errno = EINVAL;
        goto error;
    }
    if (!(value = json_dumps (o, JSON_COMPACT))) {
        errstr = "failed to encode checkpoint value";
        errno = EINVAL;
        goto error;
    }
    if (filedb_put (ctx->dbpath, key, value, strlen (value), &errstr) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to checkpoint-put request");
    free (value);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to checkpoint-put request");
    free (value);
}

/* Destroy module context.
 */
static void content_files_destroy (struct content_files *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        free (ctx->dbpath);
        free (ctx->hashfun);
        free (ctx);
        errno = saved_errno;
    }
}

/* Table of message handler callbacks registered below.
 * The topic strings in the table consist of <service name>.<method>.
 */
static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "content-backing.load",    load_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.store",   store_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.checkpoint-get", checkpoint_get_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-backing.checkpoint-put", checkpoint_put_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "content-files.stats-get",
      stats_get_cb, FLUX_ROLE_USER },
    FLUX_MSGHANDLER_TABLE_END,
};

/* Create module context and perform some initialization.
 */
static struct content_files *content_files_create (flux_t *h, bool truncate)
{
    struct content_files *ctx;
    const char *dbdir;
    const char *s;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;

    /* Some tunables:
     * - the hash function, e.g. sha1, sha256
     * - path to sqlite file
     */
    if (!(s = flux_attr_get (h, "content.hash"))
        || !(ctx->hashfun = strdup (s))
        || (ctx->hash_size = blobref_validate_hashtype (s)) < 0) {
        flux_log_error (h, "content.hash");
        goto error;
    }

    /* Prefer 'statedir' as the location for the content.files directory,
     * if set.  Otherwise use 'rundir'.  If the directory exists, the
     * instance is restarting.
     */
    if (!(dbdir = flux_attr_get (h, "statedir")))
        dbdir = flux_attr_get (h, "rundir");
    if (!dbdir) {
        flux_log_error (h, "neither statedir nor rundir are set");
        goto error;
    }
    if (asprintf (&ctx->dbpath, "%s/content.files", dbdir) < 0)
        goto error;
    if (truncate)
        (void)unlink_recursive (ctx->dbpath);
    if (mkdir (ctx->dbpath, 0700) < 0 && errno != EEXIST) {
        flux_log_error (h, "could not create %s", ctx->dbpath);
        goto error;
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    return ctx;
error:
    content_files_destroy (ctx);
    return NULL;
}

static int parse_args (flux_t *h,
                       int argc,
                       char **argv,
                       bool *testing,
                       bool *truncate)
{
    int i;
    for (i = 0; i < argc; i++) {
        if (streq (argv[i], "testing"))
            *testing = true;
        else if (streq (argv[i], "truncate"))
            *truncate = true;
        else {
            flux_log (h, LOG_ERR, "Unknown module option: %s", argv[i]);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

/* The module thread enters here with broker handle 'h' pre-connected.
 * The pattern used by most flux modules is to perform some initialization
 * including installing message handlers, then enter the flux reactor loop.
 * When the broker sends handle 'h' request messages that we registered
 * to receive during initialization, the reactor ensures that our message
 * handlers are called to deal with them.
 *
 * The reactor loop runs until it is stopped, e.g. with
 * 'flux module remove <modname>' is run on this module.
 *
 * This function should return 0, or -1 on failure with errno set.
 */
int mod_main (flux_t *h, int argc, char **argv)
{
    struct content_files *ctx;
    bool testing = false;
    bool truncate = false;
    int rc = -1;

    if (parse_args (h, argc, argv, &testing, &truncate) < 0)
        return -1;
    if (!(ctx = content_files_create (h, truncate))) {
        flux_log_error (h, "content_files_create failed");
        return -1;
    }
    if (content_register_service (h, "content-backing") < 0)
        goto done;
    if (!testing) {
        if (content_register_backing_store (h, "content-files") < 0)
            goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_unreg;
    }
    rc = 0;
done_unreg:
    if (!testing)
        (void)content_unregister_backing_store (h);
done:
    content_files_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
