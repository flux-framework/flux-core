/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <stdarg.h>
#include <jansson.h>
#include <time.h>
#include <stdarg.h>
#include <regex.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libkvs/kvs_treewalk.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/timestamp.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "builtin.h"

/* Max concurrent content load/validate requests kept in flight while walking
 * the KVS.  The walk is offline (the KVS module is not servicing it), so a
 * large window simply saturates the content backing store.
 */
#define FSCK_WALK_WINDOW 1000
/* regex for "job.xxxx.xxxx.xxxx.xxxx", where 'x' is a hex character */
#define KVS_JOB_PATH_REGEX \
    "^job.[0-9a-fA-F]{4}\\.[0-9a-fA-F]{4}\\.[0-9a-fA-F]{4}\\.[0-9a-fA-F]{4}"

struct fsck_ctx {
    flux_t *h;
    bool validate_available;
    json_t *root;
    int sequence;
    int repair_count;
    int unlink_dir_count;
    zlist_t *repair_treeobjs;
    char *hash_name;
    bool verbose;
    bool quiet;
    bool repair;
    bool job_aware;
    bool isatty;
    int errorcount;
    regex_t jobpath;
    zlist_t *repaired_jobdirs;
    zlist_t *pending_repairs;   // struct fsck_repair * recorded during walk
};

/* A repair recorded during the (read-only) tree walk and applied to ctx->root
 * afterward, since applying it mutates the in-memory root and may issue
 * blocking content loads, neither of which is safe inside a walk callback.
 * 'replacement' is the rebuilt value to file under lost+found (valref repair),
 * or NULL to only unlink the path (missing dirref).
 */
struct fsck_repair {
    char *path;
    json_t *replacement;
};

static void vmsg (struct fsck_ctx *ctx, const char *fmt, va_list ap)
{
    char buf[128];
    vsnprintf (buf, sizeof (buf), fmt, ap);
    /* no need for log_err() prefix and formatting if user is running
     * on command line */
    if (ctx->isatty)
        fprintf (stderr, "%s\n", buf);
    else
        log_msg ("%s", buf);
}

static __attribute__ ((format (printf, 2, 3)))
void warn (struct fsck_ctx *ctx, const char *fmt, ...)
{
    va_list ap;
    if (ctx->quiet)
        return;
    va_start (ap, fmt);
    vmsg (ctx, fmt, ap);
    va_end (ap);
}

static __attribute__ ((format (printf, 2, 3)))
void errmsg (struct fsck_ctx *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vmsg (ctx, fmt, ap);
    va_end (ap);
}

/* kvs_treewalk valref_request override: validate that a blob exists in the
 * backing store without transferring its content.  "validate" was added in
 * v0.77.0; fall back to "load" on older targets (see check_validate_available).
 */
static flux_future_t *fsck_valref_request (void *arg, const char *blobref)
{
    struct fsck_ctx *ctx = arg;
    const char *topic = ctx->validate_available ?
        "content-backing.validate" : "content-backing.load";
    uint32_t hash[BLOBREF_MAX_DIGEST_SIZE];
    ssize_t hash_size;

    if ((hash_size = blobref_strtohash (blobref, hash, sizeof (hash))) < 0)
        return NULL;
    return flux_rpc_raw (ctx->h, topic, hash, hash_size, 0, 0);
}

/* Build a repaired valref from the original, dropping the blobs that failed
 * to validate (blobs[i].errnum == ENOENT).  If every blob was bad the value
 * becomes an empty val object.
 */
static json_t *repair_valref (struct fsck_ctx *ctx,
                              json_t *treeobj,
                              int count,
                              const struct kvs_treewalk_blob *blobs)
{
    json_t *repaired;

    if (!(repaired = treeobj_create_valref (NULL)))
        log_err_exit ("cannot create treeobj valref");

    for (int i = 0; i < count; i++) {
        if (!blobs[i].errnum) {
            if (treeobj_append_blobref (repaired,
                                        treeobj_get_blobref (treeobj, i)) < 0)
                log_err_exit ("cannot append blobref to valref");
        }
    }

    /* if no blobrefs in valref, all blobs were bad, gotta convert to
     * empty val treeobj
     */
    if (treeobj_get_count (repaired) == 0) {
        json_decref (repaired);
        if (!(repaired = treeobj_create_val (NULL, 0)))
            log_err_exit ("cannot create treeobj val");
    }

    return repaired;
}

/* add dir if it is missing, otherwise return it.  convert dirref to
 * dir if necessary
 */
static json_t *get_dir (struct fsck_ctx *ctx,
                        json_t *treeobj_dir,
                        const char *dir_name)
{
    json_t *o;

    if (!(o = treeobj_get_entry (treeobj_dir, dir_name))) {
        if (!(o = treeobj_create_dir ()))
            log_err_exit ("cannot create treeobj dir");
        if (treeobj_insert_entry (treeobj_dir, dir_name, o) < 0)
            log_err_exit ("cannot add dir to treeobj");
    }
    else {
        if (treeobj_is_dirref (o)) {
            flux_future_t *f;
            const char *ref;
            int refcount;
            json_t *subdir, *subdirtmp;
            const void *data;
            size_t size;

            if ((refcount = treeobj_get_count (o)) < 0
                || refcount != 1
                || !(ref = treeobj_get_blobref (o, 0)))
                log_err_exit ("cannot get dirref blobref");

            if (!(f = content_load_byblobref (ctx->h,
                                              ref,
                                              CONTENT_FLAG_CACHE_BYPASS))
                || content_load_get (f, &data, &size) < 0)
                log_err_exit ("failed to load treeobj dir");

            /* deep copy b/c we may be modifying it */
            if (!(subdirtmp = treeobj_decodeb (data, size))
                || !(subdir = treeobj_deep_copy (subdirtmp))
                || treeobj_insert_entry (treeobj_dir, dir_name, subdir) < 0)
                log_err_exit ("failed to update entry from dirref to dir");

            json_decref (subdirtmp);
            o = subdir;
        }
        else if (!treeobj_is_dir (o)) {
            /* if it is not a dir or dirref, we overwrite it with a dir */
            if (!(o = treeobj_create_dir ()))
                log_err_exit ("cannot create treeobj dir");
            if (treeobj_insert_entry (treeobj_dir, dir_name, o) < 0)
                log_err_exit ("cannot add dir to treeobj");
        }
    }
    return o;
}

static void put_lost_and_found (struct fsck_ctx *ctx,
                                const char *path,
                                json_t *obj)
{
    json_t *dir = ctx->root;
    char *cpy;
    char *next, *name;

    if (asprintf (&cpy, "lost+found.%s", path) < 0)
        log_err_exit ("cannot allocate lost+found path");
    name = cpy;

    while ((next = strchr (name, '.'))) {
        json_t *subdir;

        *next++ = '\0';

        if (!treeobj_is_dir (dir))
            log_err_exit ("treeobj dir is type %s", treeobj_type_name (dir));

        subdir = get_dir (ctx, dir, name);
        name = next;
        dir = subdir;
    }

    if (treeobj_insert_entry (dir, name, obj) < 0)
        log_err_exit ("cannot insert treeobj to %s", path);

    free (cpy);
}

static void unlink_path (struct fsck_ctx *ctx, const char *path)
{
    json_t *dir = ctx->root;
    char *cpy;
    char *next, *name;

    if (!(cpy = strdup (path)))
        log_err_exit ("cannot allocate path copy");
    name = cpy;

    while ((next = strchr (name, '.'))) {
        json_t *subdir;

        *next++ = '\0';

        if (!treeobj_is_dir (dir))
            log_err_exit ("treeobj dir is type %s", treeobj_type_name (dir));

        subdir = get_dir (ctx, dir, name);
        name = next;
        dir = subdir;
    }

    if (treeobj_delete_entry (dir, name) < 0)
        log_err_exit ("cannot unlink entry %s", path);

    free (cpy);
}

static void save_kvs_job_path (struct fsck_ctx *ctx, const char *path)
{
    if (!ctx->repaired_jobdirs) {
        if (!(ctx->repaired_jobdirs = zlist_new ()))
            log_err_exit ("cannot create list for job dirs");
        zlist_comparefn (ctx->repaired_jobdirs, (zlist_compare_fn *) strcmp);
    }

    if (regexec (&ctx->jobpath, path, 0, NULL, 0) == 0) {
        /* N.B.
         * job.xxxx.xxxx.xxxx.xxxx
         * is exactly 23 chars.
         */
        char *s = strndup (path, 23);
        if (!s)
            log_err_exit ("cannot dup job path");
        if (!zlist_exists (ctx->repaired_jobdirs, (void *)s)) {
            if (zlist_append (ctx->repaired_jobdirs, s) < 0)
                log_err_exit ("failed to save job dir path");
            zlist_freefn (ctx->repaired_jobdirs, s, free, true);
        }
        else
            free (s);
    }
}

/* Record a repair to apply to ctx->root after the walk completes.  Applying
 * it now would mutate the root mid-traversal and (for lost+found) issue
 * blocking content loads from within a walk callback.
 */
static void record_repair (struct fsck_ctx *ctx,
                           const char *path,
                           json_t *replacement)
{
    struct fsck_repair *r;

    if (!ctx->pending_repairs) {
        if (!(ctx->pending_repairs = zlist_new ()))
            log_err_exit ("cannot create repair list");
    }
    if (!(r = calloc (1, sizeof (*r)))
        || !(r->path = strdup (path)))
        log_err_exit ("cannot allocate repair record");
    r->replacement = replacement; // takes ownership (may be NULL)
    if (zlist_append (ctx->pending_repairs, r) < 0)
        log_err_exit ("cannot append repair record");
}

static void fsck_repair_destroy (struct fsck_repair *r)
{
    if (r) {
        json_decref (r->replacement);
        free (r->path);
        free (r);
    }
}

/* kvs_treewalk valref_done callback: a value's blobs have all been validated.
 * Report any that are missing (one error per key; per-blob detail in verbose
 * mode) and, in repair mode, record a rebuilt value for lost+found.
 */
static void fsck_valref_done (void *arg,
                              const char *path,
                              json_t *treeobj,
                              int count,
                              const struct kvs_treewalk_blob *blobs)
{
    struct fsck_ctx *ctx = arg;
    int errorcount = 0;
    int missingcount = 0;
    int last_errnum = 0;

    for (int i = 0; i < count; i++) {
        if (!blobs[i].errnum)
            continue;
        errorcount++;
        last_errnum = blobs[i].errnum;
        if (blobs[i].errnum == ENOENT)
            missingcount++;
        if (ctx->verbose) {
            if (blobs[i].errnum == ENOENT)
                errmsg (ctx, "%s: missing blobref index=%d", path, i);
            else
                errmsg (ctx,
                        "%s: error retrieving blobref index=%d: %s",
                        path,
                        i,
                        strerror (blobs[i].errnum));
        }
    }
    if (errorcount == 0)
        return;

    /* each invalid blobref is output above in verbose mode */
    if (!ctx->verbose) {
        if (last_errnum == ENOENT)
            errmsg (ctx, "%s: missing blobref(s)", path);
        else
            errmsg (ctx,
                    "%s: error retrieving blobref(s): %s",
                    path,
                    strerror (last_errnum));
    }
    ctx->errorcount++;

    /* can only recover if errors were all bad (missing) references */
    if (ctx->repair && errorcount == missingcount) {
        record_repair (ctx, path, repair_valref (ctx, treeobj, count, blobs));
        ctx->repair_count++;
        warn (ctx, "%s repaired and moved to lost+found", path);
        if (ctx->job_aware)
            save_kvs_job_path (ctx, path);
    }
}

/* kvs_treewalk error callback: a dirref could not be loaded, decoded, or
 * validated.  Report it (the subtree is pruned by the walker) and, for a
 * missing dirref in repair mode, record the path for unlinking.
 */
static void fsck_error (void *arg,
                        const char *path,
                        enum kvs_treewalk_error error,
                        int errnum)
{
    struct fsck_ctx *ctx = arg;

    switch (error) {
        case KVS_TREEWALK_ERROR_INVALID:
            errmsg (ctx, "%s: invalid tree object", path);
            break;
        case KVS_TREEWALK_ERROR_BADCOUNT:
            errmsg (ctx, "%s: invalid dirref treeobj count", path);
            break;
        case KVS_TREEWALK_ERROR_LOAD:
            if (errnum == ENOENT)
                errmsg (ctx, "%s: missing dirref blobref", path);
            else
                errmsg (ctx,
                        "%s: error retrieving dirref blobref: %s",
                        path,
                        strerror (errnum));
            if (ctx->repair && errnum == ENOENT) {
                record_repair (ctx, path, NULL); // unlink only
                warn (ctx, "%s unlinked due to missing blobref", path);
                ctx->unlink_dir_count++;
            }
            break;
        case KVS_TREEWALK_ERROR_DECODE:
            errmsg (ctx, "%s: could not decode directory", path);
            break;
        case KVS_TREEWALK_ERROR_NOTDIR:
            errmsg (ctx, "%s: dirref references non-directory", path);
            break;
    }
    ctx->errorcount++;
}

/* kvs_treewalk visit callback: verbose path listing, matching the order the
 * old recursion printed (every object as it is reached).
 */
static void fsck_visit (void *arg, const char *path, json_t *treeobj)
{
    struct fsck_ctx *ctx = arg;
    if (ctx->verbose)
        warn (ctx, "%s", path);
}

static const struct kvs_treewalk_ops fsck_ops = {
    .visit = fsck_visit,
    .valref_request = fsck_valref_request,
    .valref_done = fsck_valref_done,
    .error = fsck_error,
};

/* Apply the repairs recorded during the walk: file the rebuilt value under
 * lost+found (valref repair) and/or unlink the bad path.  Done after the walk
 * because these mutate ctx->root and may issue blocking content loads.
 */
static void apply_repairs (struct fsck_ctx *ctx)
{
    struct fsck_repair *r;

    if (!ctx->pending_repairs)
        return;
    while ((r = zlist_pop (ctx->pending_repairs))) {
        /* A replacement (valref repair) is filed under lost+found and the
         * original path unlinked; with no replacement (missing dirref) the
         * path is only unlinked.
         */
        if (r->replacement)
            put_lost_and_found (ctx, r->path, r->replacement);
        unlink_path (ctx, r->path);
        fsck_repair_destroy (r);
    }
}

static void fsck_blobref (struct fsck_ctx *ctx, const char *blobref)
{
    struct kvs_treewalk *tw;

    if (!(tw = kvs_treewalk_create (ctx->h,
                                    blobref,
                                    '.',
                                    FSCK_WALK_WINDOW,
                                    CONTENT_FLAG_CACHE_BYPASS,
                                    &fsck_ops,
                                    ctx)))
        log_err_exit ("error creating kvs treewalk");
    /* Retain the decoded root for repair (lost+found/unlink) after the walk.
     * kvs_treewalk loads its own copy internally; this is a separate fetch of
     * the same root object.
     */
    if (ctx->repair) {
        flux_future_t *f;
        const void *buf;
        size_t buflen;
        if (!(f = content_load_byblobref (ctx->h,
                                          blobref,
                                          CONTENT_FLAG_CACHE_BYPASS))
            || content_load_get (f, &buf, &buflen) < 0) {
            errmsg (ctx,
                    "cannot load root tree object: %s",
                    future_strerror (f, errno));
            ctx->errorcount++;
            flux_future_destroy (f);
            kvs_treewalk_destroy (tw);
            return;
        }
        if (!(ctx->root = treeobj_decodeb (buf, buflen))
            || treeobj_validate (ctx->root) < 0)
            log_msg_exit ("blobref does not refer to a valid RFC 11 tree "
                          "object");
        if (!treeobj_is_dir (ctx->root))
            log_msg_exit ("root tree object is not a directory");
        flux_future_destroy (f);
    }
    if (kvs_treewalk_run (tw) < 0) {
        /* a fatal walk error (e.g. failure to load the root) is reported as
         * an error here; per-object errors went through fsck_error already */
        if (ctx->errorcount == 0) {
            errmsg (ctx,
                    "cannot load root tree object: %s",
                    strerror (errno));
            ctx->errorcount++;
        }
    }
    kvs_treewalk_destroy (tw);

    if (ctx->repair)
        apply_repairs (ctx);
}

static json_t *lookup_job_subdir (struct fsck_ctx *ctx,
                                  json_t *treeobj_dir,
                                  const char *job_path,
                                  const char *subdir)
{
    json_t *o;

    if (!(o = treeobj_get_entry (treeobj_dir, subdir))) {
        log_msg ("path %s missing directory %s", job_path, subdir);
        return NULL;
    }

    /* N.B. all dirref treeobjs in job dir path should have been
     * converted to dir treeobjs during repair
     */
    if (!treeobj_is_dir (o))
        log_err_exit ("path %s in %s is not a directory", subdir, job_path);

    return o;
}

static json_t *lookup_job_dir (struct fsck_ctx *ctx, const char *job_path)
{
    json_t *dir = ctx->root;
    json_t *subdir;
    char *path;
    char *next;

    if (!treeobj_is_dir (dir))
        log_err_exit ("treeobj dir is type %s", treeobj_type_name (dir));

    if (!(path = strdup (job_path)))
        log_err_exit ("could not duplicate job path");

    while ((next = strchr (path, '.'))) {

        *next++ = '\0';

        if (!(subdir = lookup_job_subdir (ctx, dir, job_path, path)))
            return NULL;

        path = next;
        dir = subdir;
    }

    if (!(subdir = lookup_job_subdir (ctx, dir, job_path, path))) {
        log_msg ("path %s missing in %s", path, job_path);
        return NULL;
    }

    return subdir;
}

static json_t *lookup_dir_from_dirref (struct fsck_ctx *ctx,
                                       const char *path,
                                       json_t *treeobj)
{
    flux_future_t *f = NULL;
    const void *buf;
    size_t buflen;
    json_t *treeobj_deref = NULL;
    int count;

    count = treeobj_get_count (treeobj);
    if (count != 1) {
        errmsg (ctx,
                "%s: invalid dirref treeobj count=%d",
                path,
                count);
        return NULL;
    }
    if (!(f = content_load_byblobref (ctx->h,
                                      treeobj_get_blobref (treeobj, 0),
                                      CONTENT_FLAG_CACHE_BYPASS))
        || content_load_get (f, &buf, &buflen) < 0) {
        errmsg (ctx,
                "%s: error retrieving dirref blobref: %s",
                path,
                future_strerror (f, errno));
        goto cleanup;
    }
    if (!(treeobj_deref = treeobj_decodeb (buf, buflen))) {
        errmsg (ctx, "%s: could not decode directory", path);
        goto cleanup;
    }
    if (!treeobj_is_dir (treeobj_deref)) {
        errmsg (ctx, "%s: dirref references non-directory", path);
        goto cleanup;
    }
    flux_future_destroy (f);
    return treeobj_deref;

cleanup:
    json_decref (treeobj_deref);
    flux_future_destroy (f);
    return NULL;
}

static void move_dir_lost_and_found (struct fsck_ctx *ctx,
                                     const char *path,
                                     json_t *treeobj)
{
    json_t *dict = treeobj_get_data (treeobj);
    const char *name;
    json_t *entry;

    json_object_foreach (dict, name, entry) {
        char *newpath;
        if (asprintf (&newpath, "%s.%s", path, name) < 0)
            log_msg_exit ("out of memory");
        if (treeobj_is_symlink (entry)
            || treeobj_is_val (entry)
            || treeobj_is_valref (entry))
            put_lost_and_found (ctx, newpath, entry);
        else if (treeobj_is_dir (entry))
            move_dir_lost_and_found (ctx, newpath, entry);
        else if (treeobj_is_dirref (entry)) {
            json_t *dir = lookup_dir_from_dirref (ctx, newpath, entry);
            if (dir) {
                move_dir_lost_and_found (ctx, newpath, dir);
                json_decref (dir);
            }
        }
        else
            errmsg (ctx, "Cannot move %s, unknown treeobj type", newpath);
        free (newpath);
    }
}

static void move_job_dirs (struct fsck_ctx *ctx)
{
    flux_jobid_t id;
    char *job_path;

    if (!ctx->repaired_jobdirs)
        return;

    while ((job_path = zlist_pop (ctx->repaired_jobdirs))) {
        json_t *job_dir;
        char buf[64];

        if (!(job_dir = lookup_job_dir (ctx, job_path)))
            goto next;

        move_dir_lost_and_found (ctx, job_path, job_dir);

        /* N.B. empty job directories may result from the following
         * unlink, but we ignore this, as eventual calls to
         * flux-dump(1) will clean this up.  This is no different than
         * when jobs are purged via flux-job(1).
         */
        unlink_path (ctx, job_path);

        if (flux_job_id_parse (job_path, &id) < 0
            || flux_job_id_encode (id, "f58plain", buf, sizeof (buf)) < 0)
            log_err_exit ("cannot parse kvs job id path");

        if (ctx->verbose)
            warn (ctx, "job %s moved to lost+found", buf);

    next:
        free (job_path);
    }
}

static bool kvs_is_running (struct fsck_ctx *ctx)
{
	flux_future_t *f;
	bool running = true;

	if ((f = flux_kvs_getroot (ctx->h, NULL, 0)) != NULL
		&& flux_rpc_get (f, NULL) < 0
		&& errno == ENOSYS)
		running = false;
	flux_future_destroy (f);
	return running;
}

static void save_treeobj (struct fsck_ctx *ctx, json_t *treeobj)
{
    if (zlist_append (ctx->repair_treeobjs, json_incref (treeobj)) < 0)
        log_err_exit ("failed to save treeobj");
    zlist_freefn (ctx->repair_treeobjs,
                  treeobj,
                  (zlist_free_fn *) json_decref,
                  true);
}

static void get_treeobjs (struct fsck_ctx *ctx, json_t *dir)
{
    json_t *dir_data;
    void *iter;

    if (!(dir_data = treeobj_get_data (dir)))
        log_err_exit ("cannot get dir data");

    iter = json_object_iter (dir_data);
    while (iter) {
        json_t *dir_entry = json_object_iter_value (iter);
        if (treeobj_is_dir (dir_entry)) {
            char *data;
            ssize_t datalen;
            char ref[BLOBREF_MAX_STRING_SIZE];
            json_t *dirref;

            /* depth first, so that all sub-dirs are converted to
             * dirrefs before we save off the treeobj for later
             * writing to the content store */
            get_treeobjs (ctx, dir_entry);

            save_treeobj (ctx, dir_entry);

            if (!(data = treeobj_encode (dir_entry)))
                log_err_exit ("cannot encode treeobj");
            datalen = strlen (data);
            if (blobref_hash (ctx->hash_name,
                              data,
                              datalen,
                              ref,
                              sizeof (ref)) < 0)
                log_err_exit ("cannot calculate blobref");

            if (!(dirref = treeobj_create_dirref (ref)))
                log_err_exit ("cannot create treeobj dirref");
            if (json_object_iter_set_new (dir, iter, dirref) < 0)
                log_err_exit ("cannot update dir entry to dirref");

            free (data);
        }
        iter = json_object_iter_next (dir_data, iter);
    }
}

static void store_treeobjs (struct fsck_ctx *ctx)
{
    json_t *obj;

    /* N.B. future work, parallelize this */
    while ((obj = zlist_pop (ctx->repair_treeobjs))) {
        char *data;
        ssize_t datalen;
        flux_future_t *f;
        if (!(data = treeobj_encode (obj)))
            log_err_exit ("cannot encode treeobj");
        datalen = strlen (data);
        if (!(f = content_store (ctx->h,
                                 data,
                                 datalen,
                                 CONTENT_FLAG_CACHE_BYPASS))
            || flux_rpc_get (f, NULL) < 0)
            log_err_exit ("failed to store blob to content store");
        free (data);
    }
}

static void sync_checkpoint (struct fsck_ctx *ctx)
{
    char *data;
    ssize_t datalen;
    char ref[BLOBREF_MAX_STRING_SIZE];
    flux_future_t *f;

    if (!(data = treeobj_encode (ctx->root)))
        log_err_exit ("cannot encode treeobj");
    datalen = strlen (data);

    if (blobref_hash (ctx->hash_name,
                      data,
                      datalen,
                      ref,
                      sizeof (ref)) < 0)
        log_err_exit ("cannot calculate blobref");

    if (!(f = kvs_checkpoint_commit (ctx->h,
                                     ref,
                                     ctx->sequence + 1,
                                     0.,
                                     KVS_CHECKPOINT_FLAG_CACHE_BYPASS))
        || flux_rpc_get (f, NULL) < 0)
        log_err_exit ("failed to checkpoint updated root");

    flux_future_destroy (f);

    warn (ctx, "Updated primary checkpoint to include lost+found");
}

/* "validate" support added in v0.77.0.  Use "load" for backwards
 * compatibility if "validate" is not available.
 */
static void check_validate_available (struct fsck_ctx *ctx, const char *blobref)
{
    uint32_t hash[BLOBREF_MAX_DIGEST_SIZE];
    ssize_t hash_size;
    flux_future_t *f;

    if ((hash_size = blobref_strtohash (blobref, hash, sizeof (hash))) < 0)
        log_err_exit ("cannot get hash from ref string");

    ctx->validate_available = true;
    // doesn't matter if the request is correct, we are looking for ENOSYS
    if ((f = flux_rpc_raw (ctx->h,
                           "content-backing.validate",
                           hash,
                           hash_size,
                           0,
                           0))
        && flux_rpc_get (f, NULL) < 0
        && errno == ENOSYS)
        ctx->validate_available = false;
    flux_future_destroy (f);
}

static int cmd_fsck (optparse_t *p, int ac, char *av[])
{
    struct fsck_ctx ctx = {0};
    int optindex = optparse_option_index (p);
    flux_future_t *f = NULL;
    const char *blobref;

    log_init ("flux-fsck");

    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }

    if (optparse_hasopt (p, "verbose"))
        ctx.verbose = true;
    if (optparse_hasopt (p, "quiet"))
        ctx.quiet = true;
    if (optparse_hasopt (p, "repair")) {
        ctx.repair = true;
        if (optparse_hasopt (p, "job-aware")) {
            int ret;
            ctx.job_aware = true;
            if ((ret = regcomp (&ctx.jobpath,
                                KVS_JOB_PATH_REGEX,
                                REG_EXTENDED | REG_NOSUB)) != 0) {
                char buf[256];
                regerror (ret, &ctx.jobpath, buf, sizeof (buf));
                log_msg_exit ("error compiling regex %s", buf);
            }
        }
    }

    ctx.h = builtin_get_flux_handle (p);
    ctx.isatty = isatty (STDERR_FILENO);

    if (ctx.repair && kvs_is_running (&ctx))
        log_msg_exit ("please unload kvs module before using flux-fsck --repair");

    if ((blobref = optparse_get_str (p, "rootref", NULL))) {
        if (blobref_validate (blobref) < 0)
            log_msg_exit ("invalid blobref specified");
    }
    else {
        const json_t *checkpoints;
        json_t *checkpt;
        double timestamp;
        int sequence;
        char buf[64] = "";
        struct tm tm;

        /* index 0 is most recent checkpoint */
        if (!(f = kvs_checkpoint_lookup (ctx.h, KVS_CHECKPOINT_FLAG_CACHE_BYPASS))
            || kvs_checkpoint_lookup_get (f, &checkpoints) < 0
            || !(checkpt = json_array_get (checkpoints, 0))
            || kvs_checkpoint_parse_rootref (checkpt, &blobref) < 0
            || kvs_checkpoint_parse_sequence (checkpt, &sequence) < 0
            || kvs_checkpoint_parse_timestamp (checkpt, &timestamp) < 0)
            log_msg_exit ("error fetching checkpoints: %s",
                          future_strerror (f, errno));

        if (!timestamp_from_double (timestamp, &tm, NULL))
            strftime (buf, sizeof (buf), "%Y-%m-%dT%T", &tm);
        printf ("Checking integrity of checkpoint from %s\n", buf);

        ctx.sequence = sequence;
    }

    check_validate_available (&ctx, blobref);

    fsck_blobref (&ctx, blobref);

    /* N.B. the movement of job dirs is done after fsck completes, b/c
     * during fsck scan, we have no knowledge of what has or has-not
     * been fsck-ed yet due to recursion.
     */
    if (ctx.job_aware)
        move_job_dirs (&ctx);

    flux_future_destroy (f);

    if (ctx.verbose || ctx.errorcount)
        errmsg (&ctx, "Total errors: %d", ctx.errorcount);

    if (ctx.repair) {
        if (ctx.repair_count) {
            const char *tmp;
            if (!(tmp = flux_attr_get (ctx.h, "content.hash"))
                || !(ctx.hash_name = strdup (tmp)))
                log_err_exit ("could not get content hash name");

            if (!(ctx.repair_treeobjs = zlist_new ()))
                log_err_exit ("cannot create list for treeobjs");

            get_treeobjs (&ctx, ctx.root);

            /* and gotta save root at end too */
            save_treeobj (&ctx, ctx.root);

            store_treeobjs (&ctx);

            sync_checkpoint (&ctx);
        }

        if (ctx.repair_count || ctx.verbose)
            errmsg (&ctx, "Total repairs: %d", ctx.repair_count);

        if (ctx.unlink_dir_count || ctx.verbose)
            errmsg (&ctx,
                    "Total unlinked directories: %d",
                    ctx.unlink_dir_count);
    }

    zlist_destroy (&ctx.repair_treeobjs);
    zlist_destroy (&ctx.pending_repairs);
    zlist_destroy (&ctx.repaired_jobdirs);
    json_decref (ctx.root);
    flux_close (ctx.h);

    return (ctx.errorcount ? -1 : 0);
}

static struct optparse_option fsck_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "List keys as they are being validated",
    },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Reduce output to essential information.",
    },
    { .name = "rootref", .key = 'r', .has_arg = 1, .arginfo = "BLOBREF",
      .usage = "Check integrity starting with BLOBREF",
    },
    { .name = "repair", .key = 'R', .has_arg = 0,
      .usage = "Repair recoverable keys and place in lost+found",
    },
    { .name = "job-aware", .key = 'j', .has_arg = 0,
      .usage = "Move all job data to lost+found if errors found",
    },
    OPTPARSE_TABLE_END
};

int subcommand_fsck_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "fsck",
        cmd_fsck,
        "[OPTIONS]",
        "check integrity of content store data",
        0,
        fsck_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
