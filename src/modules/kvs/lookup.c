/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <czmq.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_util_private.h"

#include "cache.h"
#include "kvsroot.h"

#include "lookup.h"

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

typedef struct {
    int depth;
    char *path_copy;            /* for internal parsing, do not use */
    char *root_ref;             /* internal copy, in case root_ref is changed */
    json_t *root_dirent;        /* root dirent for this level */
    const json_t *dirent;
    json_t *tmp_dirent;         /* tmp dirent that may need to be created */
    zlist_t *pathcomps;
} walk_level_t;

struct lookup {
    /* inputs from user */
    struct cache *cache;
    kvsroot_mgr_t *krm;
    int current_epoch;

    char *namespace;
    char *root_ref;
    int root_seq;
    bool root_ref_set_by_user;  /* if root_ref passed in by user */

    char *path;

    flux_t *h;

    uint32_t rolemask;
    uint32_t userid;

    int flags;

    void *aux;

    /* potential return values from lookup */
    json_t *val;           /* value of lookup */

    /* if valref_missing_refs is true, iterate on refs, else
     * return missing_ref string.
     */
    const json_t *valref_missing_refs;
    const char *missing_ref;

    /* for namespace callback */

    char *missing_namespace;

    int errnum;                 /* errnum if error */
    int aux_errnum;

    /* API internal */
    zlist_t *levels;
    const json_t *wdirent;       /* result after walk() */
    enum {
        LOOKUP_STATE_INIT,
        LOOKUP_STATE_CHECK_NAMESPACE,
        LOOKUP_STATE_CHECK_ROOT,
        LOOKUP_STATE_WALK_INIT,
        LOOKUP_STATE_WALK,
        LOOKUP_STATE_VALUE,
        LOOKUP_STATE_FINISHED,
    } state;
};

static bool last_pathcomp (zlist_t *pathcomps, const void *data)
{
    return (zlist_tail (pathcomps) == data);
}

/* Create list of path components, i.e. in path "a.b.c", create list
 * of "a", "b", and "c".
 */
static zlist_t *walk_pathcomps_zlist_create (walk_level_t *wl)
{
    char *next, *current;
    zlist_t *pathcomps = NULL;
    int saved_errno;

    if (!(pathcomps = zlist_new ())) {
        saved_errno = ENOMEM;
        goto error;
    }

    /* N.B. not creating memory, placing pointer to path components,
     * so cleanup is simply list destroy.
     */

    current = wl->path_copy;
    while ((next = strchr (current, '.'))) {
        *next++ = '\0';

        if (zlist_append (pathcomps, current) < 0) {
            saved_errno = ENOMEM;
            goto error;
        }

        current = next;
    }

    if (zlist_append (pathcomps, current) < 0) {
        saved_errno = ENOMEM;
        goto error;
    }

    return pathcomps;

 error:
    zlist_destroy (&pathcomps);
    errno = saved_errno;
    return NULL;
}

static void walk_level_destroy (void *data)
{
    walk_level_t *wl = (walk_level_t *)data;
    if (wl) {
        zlist_destroy (&wl->pathcomps);
        free (wl->root_ref);
        json_decref (wl->root_dirent);
        json_decref (wl->tmp_dirent);
        free (wl->path_copy);
        free (wl);
    }
}

static walk_level_t *walk_level_create (const char *root_ref,
                                        const char *path,
                                        int depth)
{
    walk_level_t *wl = calloc (1, sizeof (*wl));
    int saved_errno;

    if (!wl) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(wl->path_copy = strdup (path))) {
        saved_errno = ENOMEM;
        goto error;
    }
    wl->depth = depth;
    if (!(wl->root_ref = strdup (root_ref))) {
        saved_errno = ENOMEM;
        goto error;
    }
    if (!(wl->root_dirent = treeobj_create_dirref (root_ref))) {
        saved_errno = errno;
        goto error;
    }
    wl->dirent = wl->root_dirent;
    if (!(wl->pathcomps = walk_pathcomps_zlist_create (wl))) {
        saved_errno = errno;
        goto error;
    }

    return wl;

 error:
    walk_level_destroy (wl);
    errno = saved_errno;
    return NULL;
}

static walk_level_t *walk_levels_push (lookup_t *lh,
                                       const char *root_ref,
                                       const char *path,
                                       int depth)
{
    walk_level_t *wl;

    if (!(wl = walk_level_create (root_ref, path, depth)))
        return NULL;

    if (zlist_push (lh->levels, wl) < 0) {
        walk_level_destroy (wl);
        errno = ENOMEM;
        return NULL;
    }
    zlist_freefn (lh->levels, wl, walk_level_destroy, false);

    return wl;
}

static lookup_process_t symlink_namespace (lookup_t *lh,
                                           const char *linkpath,
                                           struct kvsroot **rootp,
                                           char **linkpathp)
{
    struct kvsroot *root = NULL;
    char *linkpath_norm = NULL;
    char *ns_prefix = NULL;
    char *p_suffix = NULL;
    lookup_process_t ret = LOOKUP_PROCESS_ERROR;
    int pret;

    if ((pret = kvs_namespace_prefix (linkpath,
                                      &ns_prefix,
                                      &p_suffix)) < 0) {
        lh->errnum = errno;
        goto done;
    }

    if (pret) {
        root = kvsroot_mgr_lookup_root (lh->krm, ns_prefix);

        if (!root) {
            free (lh->missing_namespace);
            lh->missing_namespace = ns_prefix;
            ns_prefix = NULL;
            ret = LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE;
            goto done;
        }

        if (kvsroot_check_user (lh->krm,
                                root,
                                lh->rolemask,
                                lh->userid) < 0) {
            lh->errnum = EPERM;
            goto done;
        }

        if (!(linkpath_norm = kvs_util_normalize_key (p_suffix, NULL))) {
            lh->errnum = errno;
            goto done;
        }
    }

    /* if no namespace prefix, these are set to NULL so caller knows
     * no namespace prefix
     */
    (*rootp) = root;
    (*linkpathp) = linkpath_norm;
    ret = LOOKUP_PROCESS_FINISHED;
done:
    free (ns_prefix);
    free (p_suffix);
    return ret;
}

/* If recursing, wlp will be set to new walk level pointer
 */
static lookup_process_t walk_symlink (lookup_t *lh,
                                      walk_level_t *wl,
                                      const json_t *dirent_tmp,
                                      char *current_pathcomp,
                                      walk_level_t **wlp)
{
    lookup_process_t ret = LOOKUP_PROCESS_ERROR;
    struct kvsroot *root = NULL;
    char *linkpath = NULL;
    walk_level_t *wltmp;
    const char *linkstr;

    if (!(linkstr = treeobj_get_symlink_target (dirent_tmp))) {
        lh->errnum = errno;
        goto cleanup;
    }

    /* Follow link if in middle of path or if end of path,
     * flags say we can follow it
     */
    if (!last_pathcomp (wl->pathcomps, current_pathcomp)
        || (!(lh->flags & FLUX_KVS_READLINK)
            && !(lh->flags & FLUX_KVS_TREEOBJ))) {
        lookup_process_t sret;

        if (wl->depth == SYMLINK_CYCLE_LIMIT) {
            lh->errnum = ELOOP;
            goto cleanup;
        }

        sret = symlink_namespace (lh,
                                  linkstr,
                                  &root,
                                  &linkpath);
        if (sret != LOOKUP_PROCESS_FINISHED) {
            ret = sret;
            goto cleanup;
        }

        /* Set wl->dirent, now that we've resolved any
         * namespaces in the linkstr.
         */
        wl->dirent = dirent_tmp;

        /* if symlink is root, no need to recurse, just get
         * root_dirent and continue on.
         */
        if (!strcmp (linkpath ? linkpath : linkstr, ".")) {
            if (root) {
                free (wl->tmp_dirent);
                wl->tmp_dirent = treeobj_create_dirref (root->ref);
                if (!wl->tmp_dirent) {
                    lh->errnum = errno;
                    goto cleanup;
                }
                wl->dirent = wl->tmp_dirent;
            }
            else
                wl->dirent = wl->root_dirent;
        }
        else {
            /* "recursively" determine link dirent */
            if (!(wltmp = walk_levels_push (lh,
                                            root ? root->ref : wl->root_ref,
                                            linkpath ? linkpath : linkstr,
                                            wl->depth + 1))) {
                lh->errnum = errno;
                goto cleanup;
            }

            (*wlp) = wltmp;
            goto done;
        }
    }
    else
        wl->dirent = dirent_tmp;

    (*wlp) = NULL;
done:
    ret = LOOKUP_PROCESS_FINISHED;
cleanup:
    free (linkpath);
    return ret;
}

/* Get dirent of the requested path starting at the given root.
 *
 * Return true on success or error, error code is returned in ep and
 * should be checked upon return.
 *
 * Return false if path cannot be resolved.  Return missing reference
 * in load ref, which caller should then use to load missing reference
 * into KVS cache.
 */
static lookup_process_t walk (lookup_t *lh)
{
    const json_t *dir;
    walk_level_t *wl = NULL;
    char *pathcomp;
    const json_t *dirent_tmp;

    wl = zlist_head (lh->levels);

    /* walk directories */
    while ((pathcomp = zlist_head (wl->pathcomps))) {

        /* Get directory of dirent */

        if (treeobj_is_dirref (wl->dirent)) {
            struct cache_entry *entry;
            const char *refstr;
            int refcount;

            if ((refcount = treeobj_get_count (wl->dirent)) < 0) {
                lh->errnum = errno;
                goto error;
            }

            if (refcount != 1) {
                flux_log (lh->h, LOG_ERR, "invalid dirref count: %d", refcount);
                lh->errnum = ENOTRECOVERABLE;
                goto error;
            }

            if (!(refstr = treeobj_get_blobref (wl->dirent, 0))) {
                lh->errnum = errno;
                goto error;
            }

            if (!(entry = cache_lookup (lh->cache, refstr, lh->current_epoch))
                || !cache_entry_get_valid (entry)) {
                lh->missing_ref = refstr;
                return LOOKUP_PROCESS_LOAD_MISSING_REFS;
            }
            if (!(dir = cache_entry_get_treeobj (entry))) {
                /* dirref pointed to non treeobj error, special case when
                 * root_dirent is bad, is EINVAL from user.
                 */
                flux_log (lh->h, LOG_ERR, "dirref points to non-treeobj");
                if (wl->depth == 0 && wl->dirent == wl->root_dirent)
                    lh->errnum = EINVAL;
                else
                    lh->errnum = ENOTRECOVERABLE;
                goto error;
            }
            if (!treeobj_is_dir (dir)) {
                /* dirref pointed to non-dir error, special case when
                 * root_dirent is bad, is EINVAL from user.
                 */
                if (wl->depth == 0 && wl->dirent == wl->root_dirent)
                    lh->errnum = EINVAL;
                else
                    lh->errnum = ENOTRECOVERABLE;
                goto error;
            }
        } else {
            /* Unexpected dirent type */
            if (treeobj_is_valref (wl->dirent)
                || treeobj_is_val (wl->dirent)) {
                /* don't return ENOENT or ENOTDIR, error to be
                 * determined by caller */
                goto done;
            }
            else {
                char *s = json_dumps (wl->dirent, JSON_ENCODE_ANY);
                flux_log (lh->h, LOG_ERR,
                          "%s: unknown/unexpected dirent type: "
                          "lh->path=%s pathcomp=%s wl->dirent(ptr)=%p "
                          "wl->dirent(str)=%s",
                          __FUNCTION__, lh->path, pathcomp, wl->dirent, s);
                free (s);
                lh->errnum = ENOTRECOVERABLE;
                goto error;
            }
        }

        /* Get directory reference of path component from directory */

        if (!(dirent_tmp = treeobj_peek_entry (dir, pathcomp))) {
            /* if entry does not exist, not necessarily ENOENT error,
             * let caller decide.  If error not ENOENT, return to
             * caller. */
            if (errno != ENOENT) {
                lh->errnum = errno;
                goto error;
            }

            goto done;
        }

        /* Resolve dirent if it is a link */

        if (treeobj_is_symlink (dirent_tmp)) {
            walk_level_t *wltmp = NULL;
            lookup_process_t sret;

            sret = walk_symlink (lh, wl, dirent_tmp, pathcomp, &wltmp);
            if (sret == LOOKUP_PROCESS_ERROR)
                goto error;
            else if (sret == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE)
                return LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE;
            /* else sret == LOOKUP_PROCESS_FINISHED */

            if (wltmp) {
                wl = wltmp;
                continue;
            }
        }
        else
            wl->dirent = dirent_tmp;

        if (last_pathcomp (wl->pathcomps, pathcomp)
            && wl->depth) {
            /* Unwind "recursive" step */
            do {
                walk_level_t *wl_tmp;
                char *pathcomp_tmp;

                /* Take current level off the top of the stack */
                zlist_pop (lh->levels);

                wl_tmp = zlist_head (lh->levels);
                pathcomp_tmp = zlist_head (wl_tmp->pathcomps);

                wl_tmp->dirent = wl->dirent;

                walk_level_destroy (wl);

                /* Set new current level */
                wl = wl_tmp;
                pathcomp = pathcomp_tmp;
            } while (wl->depth && last_pathcomp (wl->pathcomps, pathcomp));
        }

        zlist_remove (wl->pathcomps, pathcomp);
    }
    lh->wdirent = wl->dirent;

done:
    return LOOKUP_PROCESS_FINISHED;

error:
    lh->wdirent = NULL;
    return LOOKUP_PROCESS_ERROR;
}

lookup_t *lookup_create (struct cache *cache,
                         kvsroot_mgr_t *krm,
                         int current_epoch,
                         const char *namespace,
                         const char *root_ref,
                         int root_seq,
                         const char *path,
                         uint32_t rolemask,
                         uint32_t userid,
                         int flags,
                         flux_t *h)
{
    lookup_t *lh = NULL;
    int saved_errno;

    if (!cache || !krm || !namespace || !path) {
        errno = EINVAL;
        return NULL;
    }

    if (!(lh = calloc (1, sizeof (*lh)))) {
        saved_errno = ENOMEM;
        goto cleanup;
    }

    lh->cache = cache;
    lh->krm = krm;
    lh->current_epoch = current_epoch;

    /* must duplicate strings, user may not keep pointer alive */
    if (!(lh->namespace = strdup (namespace))) {
        saved_errno = ENOMEM;
        goto cleanup;
    }

    if (!(lh->path = kvs_util_normalize_key (path, NULL))) {
        saved_errno = errno;
        goto cleanup;
    }

    if (root_ref) {
        if (!(lh->root_ref = strdup (root_ref))) {
            saved_errno = ENOMEM;
            goto cleanup;
        }
        lh->root_seq = root_seq;
        lh->root_ref_set_by_user = true;
    }

    lh->h = h;

    lh->rolemask = rolemask;
    lh->userid = userid;
    lh->flags = flags;

    lh->val = NULL;
    lh->valref_missing_refs = NULL;
    lh->missing_ref = NULL;
    lh->errnum = 0;

    if (!(lh->levels = zlist_new ())) {
        saved_errno = ENOMEM;
        goto cleanup;
    }

    lh->wdirent = NULL;
    lh->state = LOOKUP_STATE_INIT;

    return lh;

 cleanup:
    lookup_destroy (lh);
    errno = saved_errno;
    return NULL;
}

void lookup_destroy (lookup_t *lh)
{
    if (lh) {
        free (lh->namespace);
        free (lh->root_ref);
        free (lh->path);
        json_decref (lh->val);
        free (lh->missing_namespace);
        zlist_destroy (&lh->levels);
        free (lh);
    }
}

int lookup_get_errnum (lookup_t *lh)
{
    if (lh) {
        if (lh->state == LOOKUP_STATE_FINISHED)
            return lh->errnum;
        if (lh->state == LOOKUP_STATE_CHECK_NAMESPACE
            || lh->state == LOOKUP_STATE_CHECK_ROOT
            || lh->state == LOOKUP_STATE_WALK
            || lh->state == LOOKUP_STATE_VALUE)
            return EAGAIN;
    }
    return EINVAL;
}

int lookup_get_aux_errnum (lookup_t *lh)
{
    if (lh)
        return lh->aux_errnum;
    return EINVAL;
}

int lookup_set_aux_errnum (lookup_t *lh, int errnum)
{
    if (lh) {
        lh->aux_errnum = errnum;
        return lh->aux_errnum;
    }
    return EINVAL;
}

json_t *lookup_get_value (lookup_t *lh)
{
    if (lh
        && lh->state == LOOKUP_STATE_FINISHED
        && lh->errnum == 0)
        return json_incref (lh->val);
    return NULL;
}

int lookup_iter_missing_refs (lookup_t *lh, lookup_ref_f cb, void *data)
{
    if (lh
        && (lh->state == LOOKUP_STATE_CHECK_ROOT
            || lh->state == LOOKUP_STATE_WALK
            || lh->state == LOOKUP_STATE_VALUE)) {
        if (lh->valref_missing_refs) {
            int refcount, i;

            if (!treeobj_is_valref (lh->valref_missing_refs)) {
                errno = ENOTRECOVERABLE;
                return -1;
            }

            refcount = treeobj_get_count (lh->valref_missing_refs);
            assert (refcount > 0);

            for (i = 0; i < refcount; i++) {
                struct cache_entry *entry;
                const char *ref;

                if (!(ref = treeobj_get_blobref (lh->valref_missing_refs, i)))
                    return -1;

                if (!(entry = cache_lookup (lh->cache, ref, lh->current_epoch))
                    || !cache_entry_get_valid (entry)) {

                    /* valref points to raw data, raw_data flag is always
                     * true */
                    if (cb (lh, ref, data) < 0)
                        return -1;
                }
            }
        }
        else {
            if (cb (lh, lh->missing_ref, data) < 0)
                return -1;
        }
        return 0;
    }
    errno = EINVAL;
    return -1;
}

const char *lookup_missing_namespace (lookup_t *lh)
{
   if (lh
       && (lh->state == LOOKUP_STATE_CHECK_NAMESPACE
           || lh->state == LOOKUP_STATE_WALK)) {
       return lh->missing_namespace;
    }
    errno = EINVAL;
    return NULL;
}

int lookup_get_current_epoch (lookup_t *lh)
{
    if (lh)
        return lh->current_epoch;
    return -1;
}

const char *lookup_get_namespace (lookup_t *lh)
{
    if (lh)
        return lh->namespace;
    return NULL;
}

const char *lookup_get_root_ref (lookup_t *lh)
{
    if (lh && lh->state == LOOKUP_STATE_FINISHED)
        return lh->root_ref;
    return NULL;
}

int lookup_get_root_seq (lookup_t *lh)
{
    if (lh && lh->state == LOOKUP_STATE_FINISHED)
        return lh->root_seq;
    return -1;
}

int lookup_set_current_epoch (lookup_t *lh, int epoch)
{
    if (lh) {
        lh->current_epoch = epoch;
        return 0;
    }
    return -1;
}

static int namespace_still_valid (lookup_t *lh)
{
    struct kvsroot *root;

    /* If user set root_ref, no need to do this check */
    if (lh->root_ref_set_by_user)
        return 0;

    if (!(root = kvsroot_mgr_lookup_root_safe (lh->krm, lh->namespace))) {
        lh->errnum = ENOTSUP;
        return -1;
    }

    /* Small chance root removed, then re-inserted, check security
     * checks again */

    if (kvsroot_check_user (lh->krm,
                            root,
                            lh->rolemask,
                            lh->userid) < 0) {
        lh->errnum = errno;
        return -1;
    }

    return 0;
}

/* return 0 on success, -1 on failure.  On success, stall should be
 * checked */
static int get_single_blobref_valref_value (lookup_t *lh, bool *stall)
{
    struct cache_entry *entry;
    const char *reftmp;
    const void *valdata;
    int len;

    if (!(reftmp = treeobj_get_blobref (lh->wdirent, 0))) {
        lh->errnum = errno;
        return -1;
    }
    if (!(entry = cache_lookup (lh->cache, reftmp, lh->current_epoch))
        || !cache_entry_get_valid (entry)) {
        lh->valref_missing_refs = lh->wdirent;
        (*stall) = true;
        return 0;
    }
    if (cache_entry_get_raw (entry, &valdata, &len) < 0) {
        flux_log (lh->h, LOG_ERR, "cache_entry_get_raw");
        lh->errnum = ENOTRECOVERABLE;
        return -1;
    }
    if (!(lh->val = treeobj_create_val (valdata, len))) {
        lh->errnum = errno;
        return -1;
    }
    (*stall) = false;
    return 0;
}

static int get_multi_blobref_valref_length (lookup_t *lh, int refcount,
                                            int *total_len, bool *stall)
{
    struct cache_entry *entry;
    const char *reftmp;
    int total = 0;
    int len;
    int i;

    for (i = 0; i < refcount; i++) {
        if (!(reftmp = treeobj_get_blobref (lh->wdirent, i))) {
            lh->errnum = errno;
            return -1;
        }
        if (!(entry = cache_lookup (lh->cache, reftmp, lh->current_epoch))
            || !cache_entry_get_valid (entry)) {
            lh->valref_missing_refs = lh->wdirent;
            (*stall) = true;
            return 0;
        }

        if (cache_entry_get_raw (entry, NULL, &len) < 0) {
            flux_log (lh->h, LOG_ERR, "cache_entry_get_raw");
            lh->errnum = ENOTRECOVERABLE;
            return -1;
        }

        /* cache ensures all lens >= 0 */
        if (len > (INT_MAX - total)) {
            lh->errnum = EOVERFLOW;
            return -1;
        }
        total += len;
    }

    (*total_len) = total;
    (*stall) = false;
    return 0;
}

static char *get_multi_blobref_valref_data (lookup_t *lh, int refcount,
                                            int total_len)
{
    struct cache_entry *entry;
    const char *reftmp;
    const void *valdata;
    int len;
    char *valbuf = NULL;
    int pos = 0;
    int i;

    if (!(valbuf = malloc (total_len))) {
        lh->errnum = errno;
        return NULL;
    }

    for (i = 0; i < refcount; i++) {
        int ret;

        /* this function should only be called if all cache entries
         * known to be valid & raw, thus assert checks below */

        reftmp = treeobj_get_blobref (lh->wdirent, i);
        assert (reftmp);

        entry = cache_lookup (lh->cache, reftmp, lh->current_epoch);
        assert (entry);
        assert (cache_entry_get_valid (entry));

        ret = cache_entry_get_raw (entry, &valdata, &len);
        assert (ret == 0);

        memcpy (valbuf + pos, valdata, len);
        pos += len;
        assert (pos <= total_len);
    }

    return valbuf;
}

/* return 0 on success, -1 on failure.  On success, stall should be
 * check */
static int get_multi_blobref_valref_value (lookup_t *lh, int refcount,
                                           bool *stall)
{
    char *valbuf = NULL;
    int total_len = 0;
    int rc = -1;

    if (get_multi_blobref_valref_length (lh, refcount, &total_len, stall) < 0)
        goto done;

    if ((*stall) == true) {
        rc = 0;
        goto done;
    }

    if (!(valbuf = get_multi_blobref_valref_data (lh, refcount, total_len)))
        goto done;

    if (!(lh->val = treeobj_create_val (valbuf, total_len))) {
        lh->errnum = errno;
        goto done;
    }

    (*stall) = false;
    rc = 0;
done:
    free (valbuf);
    return rc;
}

lookup_process_t lookup (lookup_t *lh)
{
    const json_t *valtmp = NULL;
    const char *reftmp;
    struct cache_entry *entry;
    bool is_replay = false;
    int refcount;

    if (!lh) {
        errno = EINVAL;
        return LOOKUP_PROCESS_ERROR;
    }

    if (lh->errnum)
        return LOOKUP_PROCESS_ERROR;

    if (lh->state != LOOKUP_STATE_INIT
        && lh->state != LOOKUP_STATE_FINISHED)
        is_replay = true;

    switch (lh->state) {
        case LOOKUP_STATE_INIT:
            lh->state = LOOKUP_STATE_CHECK_NAMESPACE;
            /* fallthrough */
        case LOOKUP_STATE_CHECK_NAMESPACE:
            /* If user did not specify root ref, must get from
             * namespace
             */
            if (!lh->root_ref) {
                struct kvsroot *root;

                root = kvsroot_mgr_lookup_root_safe (lh->krm, lh->namespace);

                if (!root) {
                    free (lh->missing_namespace);
                    if (!(lh->missing_namespace = strdup (lh->namespace))) {
                        lh->errnum = ENOMEM;
                        goto error;
                    }
                    return LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE;
                }

                if (kvsroot_check_user (lh->krm,
                                        root,
                                        lh->rolemask,
                                        lh->userid) < 0) {
                    lh->errnum = errno;
                    goto error;
                }

                /* copy instead of storing pointer, always chance
                 * namespace could timeout or be removed when
                 * stalling */
                if (!(lh->root_ref = strdup (root->ref))) {
                    lh->errnum = ENOMEM;
                    goto error;
                }
                lh->root_seq = root->seq;
            }

            lh->state = LOOKUP_STATE_CHECK_ROOT;
            /* fallthrough */
        case LOOKUP_STATE_CHECK_ROOT:

            if (is_replay) {
                if (namespace_still_valid (lh) < 0)
                    goto error;
            }

            /* special case root */
            if (!strcmp (lh->path, ".")) {
                if ((lh->flags & FLUX_KVS_TREEOBJ)) {
                    if (!(lh->val = treeobj_create_dirref (lh->root_ref))) {
                        lh->errnum = errno;
                        goto error;
                    }
                } else {
                    if (!(lh->flags & FLUX_KVS_READDIR)) {
                        lh->errnum = EISDIR;
                        goto error;
                    }
                    if (!(entry = cache_lookup (lh->cache,
                                                lh->root_ref,
                                                lh->current_epoch))
                        || !cache_entry_get_valid (entry)) {
                        lh->missing_ref = lh->root_ref;
                        return LOOKUP_PROCESS_LOAD_MISSING_REFS;
                    }
                    if (!(valtmp = cache_entry_get_treeobj (entry))) {
                        flux_log (lh->h, LOG_ERR,
                                  "root_ref points to non-treeobj");
                        lh->errnum = EINVAL;
                        goto error;
                    }
                    if (!treeobj_is_dir (valtmp)) {
                        /* root_ref points to not dir */
                        lh->errnum = ENOTRECOVERABLE;
                        goto error;
                    }
                    if (!(lh->val = treeobj_deep_copy (valtmp))) {
                        lh->errnum = errno;
                        goto error;
                    }
                }
                goto done;
            }

            lh->state = LOOKUP_STATE_WALK_INIT;
            /* fallthrough */
        case LOOKUP_STATE_WALK_INIT:
            /* initialize walk - first depth is level 0 */

            if (!walk_levels_push (lh, lh->root_ref, lh->path, 0)) {
                lh->errnum = errno;
                goto error;
            }

            lh->state = LOOKUP_STATE_WALK;
            /* fallthrough */
        case LOOKUP_STATE_WALK:
        {
            lookup_process_t lret;

            if (is_replay) {
                if (namespace_still_valid (lh) < 0)
                    goto error;
            }

            lret = walk (lh);

            if (lret == LOOKUP_PROCESS_ERROR)
                goto error;
            else if (lret == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE)
                return LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE;
            else if (lret == LOOKUP_PROCESS_LOAD_MISSING_REFS)
                return LOOKUP_PROCESS_LOAD_MISSING_REFS;
            else if (!lh->wdirent) {
                //lh->errnum = ENOENT;
                goto done; /* a NULL response is not necessarily an error */
            }

            lh->state = LOOKUP_STATE_VALUE;
            /* fallthrough */
        }
        case LOOKUP_STATE_VALUE:
            if (is_replay) {
                if (namespace_still_valid (lh) < 0)
                    goto error;
            }

            if ((lh->flags & FLUX_KVS_TREEOBJ)) {
                if (!(lh->val = treeobj_deep_copy (lh->wdirent))) {
                    lh->errnum = errno;
                    goto error;
                }
                goto done;
            }

            if (treeobj_is_dirref (lh->wdirent)) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto error;
                }
                if (!(lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = EISDIR;
                    goto error;
                }
                if ((refcount = treeobj_get_count (lh->wdirent)) < 0) {
                    lh->errnum = errno;
                    goto error;
                }
                if (refcount != 1) {
                    flux_log (lh->h, LOG_ERR, "invalid dirref count: %d",
                              refcount);
                    lh->errnum = ENOTRECOVERABLE;
                    goto error;
                }
                if (!(reftmp = treeobj_get_blobref (lh->wdirent, 0))) {
                    lh->errnum = errno;
                    goto error;
                }
                if (!(entry = cache_lookup (lh->cache, reftmp,
                                            lh->current_epoch))
                    || !cache_entry_get_valid (entry)) {
                    lh->missing_ref = reftmp;
                    return LOOKUP_PROCESS_LOAD_MISSING_REFS;
                }
                if (!(valtmp = cache_entry_get_treeobj (entry))) {
                    flux_log (lh->h, LOG_ERR, "dirref points to non-treeobj");
                    lh->errnum = ENOTRECOVERABLE;
                    goto error;
                }
                if (!treeobj_is_dir (valtmp)) {
                    /* dirref points to not dir */
                    lh->errnum = ENOTRECOVERABLE;
                    goto error;
                }
                if (!(lh->val = treeobj_deep_copy (valtmp))) {
                    lh->errnum = errno;
                    goto error;
                }
            } else if (treeobj_is_valref (lh->wdirent)) {
                bool stall;

                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto error;
                }
                if ((lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = ENOTDIR;
                    goto error;
                }
                if ((refcount = treeobj_get_count (lh->wdirent)) < 0) {
                    lh->errnum = errno;
                    goto error;
                }
                if (!refcount) {
                    flux_log (lh->h, LOG_ERR, "invalid valref count: %d",
                              refcount);
                    lh->errnum = ENOTRECOVERABLE;
                    goto error;
                }
                if (refcount == 1) {
                    if (get_single_blobref_valref_value (lh, &stall) < 0)
                        goto error;
                    if (stall)
                        return LOOKUP_PROCESS_LOAD_MISSING_REFS;
                }
                else {
                    if (get_multi_blobref_valref_value (lh,
                                                        refcount,
                                                        &stall) < 0)
                        goto error;
                    if (stall)
                        return LOOKUP_PROCESS_LOAD_MISSING_REFS;
                }
            } else if (treeobj_is_dir (lh->wdirent)) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto error;
                }
                if (!(lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = EISDIR;
                    goto error;
                }
                if (!(lh->val = treeobj_deep_copy (lh->wdirent))) {
                    lh->errnum = errno;
                    goto error;
                }
            } else if (treeobj_is_val (lh->wdirent)) {
                if ((lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EINVAL;
                    goto error;
                }
                if ((lh->flags & FLUX_KVS_READDIR)) {
                    lh->errnum = ENOTDIR;
                    goto error;
                }
                if (!(lh->val = treeobj_deep_copy (lh->wdirent))) {
                    lh->errnum = errno;
                    goto error;
                }
            } else if (treeobj_is_symlink (lh->wdirent)) {
                /* this should be "impossible" */
                if (!(lh->flags & FLUX_KVS_READLINK)) {
                    lh->errnum = EPROTO;
                    goto error;
                }
                if (lh->flags & FLUX_KVS_READDIR) {
                    lh->errnum = ENOTDIR;
                    goto error;
                }
                if (!(lh->val = treeobj_deep_copy (lh->wdirent))) {
                    lh->errnum = errno;
                    goto error;
                }
            } else {
                char *s = json_dumps (lh->wdirent, JSON_ENCODE_ANY);
                flux_log (lh->h, LOG_ERR, "%s: corrupt dirent: %p, %s",
                          __FUNCTION__, lh->wdirent, s);
                free (s);
                lh->errnum = ENOTRECOVERABLE;
                goto error;
            }
            /* val now contains the requested object (copied) */
            break;
        case LOOKUP_STATE_FINISHED:
            break;
        default:
            flux_log (lh->h, LOG_ERR, "%s: invalid state %d",
                      __FUNCTION__, lh->state);
            lh->errnum = ENOTRECOVERABLE;
            goto error;
    }

done:
    lh->state = LOOKUP_STATE_FINISHED;
    return LOOKUP_PROCESS_FINISHED;

error:
    lh->state = LOOKUP_STATE_FINISHED;
    return LOOKUP_PROCESS_ERROR;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
