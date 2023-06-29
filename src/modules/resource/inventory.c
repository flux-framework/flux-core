/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* inventory.c - container for instance resources
 *
 * Instance resources (R) are initialized here.
 *
 * Three main sources of R were described in flux-framework/flux-core#3238:
 * 1. configured resources (e.g. system instance)
 * 2. resources assigned to instance by enclosing instance
 * 3. dynamic discovery
 *
 * This module captures R internally, commits it to 'resource.R' in the KVS,
 * and posts the resource-define event to resource.eventlog.
 *
 * The three cases above work as follows:
 *
 * Case 1 (method=configuration)
 * -----------------------------
 * TOML config specifies [resource] path, pointing to R.  R is parsed
 * and is "re-ranked" if the 'hostlist' broker attribute defines a
 * mapping of ranks to hostnames AND there exists a [bootstrap] config.
 * (Sys admins are not required to regenerate R when they reassign broker
 * ranks via [bootstrap]).
 *
 * R is configured on all ranks during resource module load.  On rank 0,
 * resource.R is committed to the KVS, and the resource-define event is
 * posted to resource.eventlog.
 *
 * topo.c ensures that configured resources match the hwloc topology on
 * all ranks.  If there are missing resources, offending ranks are drained.
 *
 * Case 2 (method=job-info)
 * ------------------------
 * On the rank 0 broker, if the 'parent-uri' broker attribute is defined,
 * a connection is made to the parent broker, and R is read from the
 * job-info module.  This R was assigned to the instance by the enclosing
 * instance scheduler, and includes ranks representing brokers in the
 * enclosing instance.
 *
 * If the same number of ranks are defined in R as there are brokers in this
 * instance, then the ranks are renumbered to be contiguous starting from
 * zero.  If a different number of ranks are defined (e.g. launching multiple
 * brokers per node), we bail out of case 2 and fall through to case 3.
 *
 * On rank 0, resource.R is committed to the KVS, and the resource-define
 * event is posted to resource.eventlog.  The other ranks request R from
 * their TBON parent using the 'resource.get' RPC, synchronously, so R
 * is defined on all ranks after module load completes.
 *
 * Case 3 (method=dynamic-discovery)
 * ---------------------------------
 * If inventory_create() returns without defining R, topo.c initiates
 * resource discovery.  Module load may complete before R is defined.
 *
 * Once the topology has been reduced to R on rank 0, resource.R is committed
 * to the KVS, and the resource-define event is posted to resource.eventlog.
 * This event serves as synchronization to indicate that R is now available.
 * acquire.c watches for this event.
 *
 * Test Features
 * -------------
 * When the module is reloaded on rank 0, if resource.R is found in the KVS,
 * it is reused.  This allows the rank 0 resource module to be reloaded in test
 * without the need to go through resource discovery (case 3) or interacting
 * with enclosing instance (case 2) again.  An existing resource.R is ignored
 * if resources are set by configuration (case 1).
 *
 * Tests that require fake resources may set them with
 * 'flux resource reload PATH', where PATH points to a file containing R.
 * Alternatively, use 'flux resource reload -x DIR' to load <rank>.xml files
 * and use them to generate R.
 *
 * It's also possible to fake resources by placing them in resource.R and
 * then (re-) loading the resource module.  This is how the sharness 'job'
 * personality fakes resources.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include <src/common/librlist/rlist.h>
#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"

#include "rutil.h"
#include "resource.h"
#include "reslog.h"
#include "acquire.h"
#include "inventory.h"


struct inventory {
    struct resource_ctx *ctx;

    json_t *R;
    char *method;

    flux_future_t *f;
    flux_msg_handler_t **handlers;
};

static int rank_from_key (const char *key);

static int inventory_put_finalize (struct inventory *inv)
{
    const char *method = flux_future_aux_get (inv->f, "method");
    int rc = -1;

    if (flux_future_get (inv->f, NULL) < 0) {
        flux_log_error (inv->ctx->h, "error committing R to KVS");
        goto done;
    }
    if (reslog_post_pack (inv->ctx->reslog,
                          NULL,
                          0.,
                          "resource-define",
                          "{s:s}",
                          "method",
                          method) < 0) {
        flux_log_error (inv->ctx->h, "error posting resource-define event");
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (inv->f);
    inv->f = NULL;
    return rc;
}

static void inventory_put_continuation (flux_future_t *f, void *arg)
{
    struct inventory *inv = arg;

    (void)inventory_put_finalize (inv);
}

/* (rank 0) Commit resource.R to the KVS, then upon completion,
 * post resource-define event to resource.eventlog.
 */
int inventory_put (struct inventory *inv, json_t *R, const char *method)

{
    flux_kvs_txn_t *txn;
    char *cpy;

    if (inv->ctx->rank != 0) {
        errno = EINVAL;
        return -1;
    }
    if (inv->R) {
        errno = EEXIST;
        return -1;
    }
    if (!(txn = flux_kvs_txn_create ()))
        return -1;
    if (flux_kvs_txn_pack (txn, 0, "resource.R", "O", R) < 0)
        goto error;
    if (!(inv->f = flux_kvs_commit (inv->ctx->h, NULL, 0, txn)))
        goto error;
    if (flux_future_then (inv->f, -1, inventory_put_continuation, inv) < 0)
        goto error;
    if (flux_future_aux_set (inv->f, "method", (void *)method, NULL) < 0)
        goto error;
    if (!(cpy = strdup (method)))
        goto error;
    inv->method = cpy;
    inv->R = json_incref (R);
    flux_kvs_txn_destroy (txn);
    return 0;
error:
    flux_kvs_txn_destroy (txn);
    return -1;
}

json_t *inventory_get (struct inventory *inv)
{
    if (!inv->R) {
        errno = ENOENT;
        return NULL;
    }
    return inv->R;
}

const char *inventory_get_method (struct inventory *inv)
{
    if (!inv->method) {
        errno = ENOENT;
        return NULL;
    }
    return inv->method;
}

struct idset *inventory_targets_to_ranks (struct inventory *inv,
                                          const char *targets,
                                          flux_error_t *errp)
{
    struct idset *ids;

    if (!inv->R) {
        errno = ENOENT;
        return NULL;
    }
    if (!(ids = idset_decode (targets))) {
        /*  Not a valid idset, maybe an RFC29 Hostlist
         */
        flux_error_t err;
        struct rlist *rl = rlist_from_json (inv->R, NULL);
        ids = rlist_hosts_to_ranks (rl, targets, &err);
        rlist_destroy (rl);
        if (!ids) {
            errprintf (errp, "invalid targets: %s", err.text);
            errno = EINVAL;
            return NULL;
        }
    }
    return ids;
}

/* Test if [bootstrap] table exists in the configuration.
 * If it does then we can assume that the 'hostlist' attribute was
 * derived from the TOML config, and may be used to re-rank a configured R.
 */
static bool conf_has_bootstrap (flux_t *h)
{
    json_t *o;

    if (flux_conf_unpack (flux_get_conf (h),
                          NULL,
                          "{s:o}",
                          "bootstrap", &o) < 0)
        return false;
    return true;
}

static int convert_R_conf (flux_t *h, json_t *conf_R, json_t **Rp)
{
    json_error_t e;
    struct rlist *rl;
    flux_error_t err;
    json_t *R;
    const char *hosts;

    if (!(rl = rlist_from_json (conf_R, &e))) {
        flux_log (h, LOG_ERR, "error parsing R: %s", e.text);
        errno = EINVAL;
        return -1;
    }
    if (conf_has_bootstrap (h)) {
        if (!(hosts = flux_attr_get (h, "hostlist"))) {
            flux_log_error (h, "Unable to get hostlist attribute");
            goto error;
        }
        err.text[0] = '\0';
        if (rlist_rerank (rl, hosts, &err) < 0) {
            flux_log (h, LOG_ERR, "error reranking R: %s", err.text);
            /*
             *  rlist_rerank() repurposes errno like EOVERFLOW and ENOSPC,
             *   but this may cause confusion when logging an error
             *   return from this function. Since the specific error has
             *   already been printed, reset errno to EINVAL so that
             *   the calling function simply prints "Invalid argument".
             */
            errno = EINVAL;
            goto error;
        }
    }
    if (!(R = rlist_to_R (rl))) {
        errno = ENOMEM;
        goto error;
    }
    rlist_destroy (rl);
    *Rp = R;
    return 0;
error:
    rlist_destroy (rl);
    return -1;
}

static bool no_duplicates (const char *hosts)
{
    struct hostlist *hl = hostlist_decode (hosts);
    bool result = false;

    if (hl) {
        int count = hostlist_count (hl);
        hostlist_uniq (hl);
        if (hostlist_count (hl) == count)
            result = true;
    }

    hostlist_destroy (hl);

    return result;
}

/* Derive resource object from R, normalizing broker ranks to origin.
 * Return success (0) but leave *Rp alone if conversion cannot be performed,
 * thus *Rp should be set to NULL before calling this function.
 * On failure return -1 (errno is not set).
 */
static int convert_R (flux_t *h, const char *job_R, int size, json_t **Rp)
{
    struct rlist *rl;
    json_t *R;
    struct idset *ranks;
    const char *hosts;
    int count;
    int rc = -1;
    flux_error_t err;

    if (!(rl = rlist_from_R (job_R)))
        return -1;
    if (!(ranks = rlist_ranks (rl)))
        goto error;
    count = idset_count (ranks);
    if (count != size) {
        flux_log (h,
                  LOG_DEBUG,
                  "cannot map %d ranks of R to %d brokers, "
                  "falling back to discovery",
                  count,
                  size);
        goto noconvert;
    }
    /*  If we have an assigned hostlist and there is no more than
     *   one broker per rank (i.e. no duplicates), then rerank R
     *   based on the assigned hostlist.
     */
    if ((hosts = flux_attr_get (h, "hostlist"))
        && no_duplicates (hosts)) {

        /*  Allow rlist_rerank() to fail here. This could be due to a fake
         *   R used in testing, or other conditions where it won't make
         *   sense to apply the re-ranking anyway. Just issue a warning
         *   and continue on failure.
         */
        err.text[0] = '\0';
        if (rlist_rerank (rl, hosts, &err) < 0)
            flux_log (h, LOG_DEBUG,
                      "Warning: rerank of R failed: %s", err.text);
    }
    /*  Also always remap ids to zero origin
     */
    if (rlist_remap (rl) < 0)
        goto error;
    if (!(R = rlist_to_R (rl)))
        goto error;
    *Rp = R;
noconvert:
    rc = 0;
error:
    idset_destroy (ranks);
    rlist_destroy (rl);
    return rc;
}

static int get_from_job_info (struct inventory *inv, const char *key)
{
    flux_t *h = inv->ctx->h;
    flux_t *parent_h;
    const char *uri;
    const char *jobid;
    flux_jobid_t id;
    const char *job_R;
    flux_future_t *f = NULL;
    json_t *R = NULL;
    int rc = -1;

    if (!(uri = flux_attr_get (h, "parent-uri"))
            || !(jobid = flux_attr_get (h, "jobid")))
        return 0;
    if (flux_job_id_parse (jobid, &id) < 0) {
        flux_log_error (h, "error decoding jobid %s", jobid);
        return -1;
    }
    if (!(parent_h = flux_open (uri, 0))) {
        flux_log_error (h, "error opening %s", uri);
        goto done;
    }
    if (!(f = flux_rpc_pack (parent_h,
                             "job-info.lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:[s] s:i}",
                             "id", id,
                             "keys", key,
                             "flags", 0))) {
        flux_log_error (h, "error sending request to enclosing instance");
        goto done;
    }
    if (flux_rpc_get_unpack (f, "{s:s}", "R", &job_R) < 0) {
        flux_log_error (h, "lookup R from enclosing instance KVS");
        goto done;
    }
    if (convert_R (h, job_R, inv->ctx->size, &R) < 0) {
        flux_log (h, LOG_ERR, "fatal error while normalizing R");
        errno = EINVAL;
        goto done;
    }
    if (R) { // R = NULL if no conversion possible (fall through to discovery)
        if (inventory_put (inv, R, "job-info") < 0)
            goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    flux_close (parent_h);
    json_decref (R);
    return rc;
}

static int get_from_kvs (struct inventory *inv, const char *key)
{
    flux_future_t *f;
    json_t *o;
    int rc = -1;

    if (!(f = flux_kvs_lookup (inv->ctx->h, NULL, 0, key)))
        return -1;
    if (flux_kvs_lookup_get_unpack (f, "o", &o) < 0) {
        if (errno == ENOENT)
            rc = 0;
        goto done;
    }
    if (inventory_put (inv, o, "kvs") < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static void resource_get (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct inventory *inv = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!inv->R) {
        errno = ENOENT;
        goto error;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:O s:s}",
                           "R",
                           inv->R,
                           "method",
                           inv->method) < 0)
        flux_log_error (h, "error responding to resource.get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to resource.get request");
}

static int get_from_upstream (struct inventory *inv)
{
    flux_t *h = inv->ctx->h;
    flux_future_t *f;
    json_t *R;
    const char *method;
    int rc = -1;

    if (!(f = flux_rpc (h,"resource.get", NULL, FLUX_NODEID_UPSTREAM, 0)))
        return -1;
    if (flux_rpc_get_unpack (f,
                             "{s:o s:s}",
                             "R",
                             &R,
                             "method",
                             &method) < 0) {
        if (errno == ENOENT)
            rc = 0;
        goto done;
    }
    else {
        if (!(inv->method = strdup (method)))
            goto done;
        inv->R = json_incref (R);
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int rank_from_key (const char *key)
{
    char *endptr;
    int rank;

    errno = 0;
    rank = strtoul (key, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        return -1;
    return rank;
}

static json_t *resobj_from_xml (json_t *xml)
{
    const char *key;
    json_t *value;
    const char *s;
    int rank;
    struct rlist *rl = NULL;
    struct rlist *rl2;
    json_t *R;

    json_object_foreach (xml, key, value) {
        if ((rank = rank_from_key (key)) < 0)
            goto error;
        if (!(s = json_string_value (value)) || strlen (s) == 0)
            goto error;
        if (!(rl2 = rlist_from_hwloc (rank, s)))
            goto error;
        if (rl) {
            if (rlist_append (rl, rl2) < 0) {
                rlist_destroy (rl2);;
                goto error;
            }
            rlist_destroy (rl2);
        }
        else
            rl = rl2;
    }
    if (!(R = rlist_to_R (rl)))
        goto error;
    rlist_destroy (rl);
    return R;
error:
    errno = EINVAL;
    rlist_destroy (rl);
    return NULL;
}

static int resobj_check_ranks (json_t *resobj, int size)
{
    json_error_t e;
    struct rlist *rl;
    struct idset *ids = NULL;
    unsigned long last;
    int rc = -1;

    if (!(rl = rlist_from_json (resobj, &e)))
        goto done;
    if (!(ids = rlist_ranks (rl)))
        goto done;
    last = idset_last (ids);
    if (last != IDSET_INVALID_ID && last >= size)
        goto done;
    rc = 0;
done:
    idset_destroy (ids);
    rlist_destroy (rl);
    return rc;
}

int inventory_get_size (struct inventory *inv)
{
    struct rlist *rl = NULL;
    struct idset *ids = NULL;
    int count = 0;

    if (inv != NULL
        && inv->R != NULL
        && (rl = rlist_from_json (inv->R, NULL))
        && (ids = rlist_ranks (rl))) {
        count = idset_count (ids);
    }
    idset_destroy (ids);
    rlist_destroy (rl);
    return count;
}

static void resource_reload (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct inventory *inv = arg;
    flux_error_t error;
    const char *errstr = NULL;
    const char *path;
    int xml_flag;
    int force_flag;
    json_t *resobj = NULL;
    json_t *xml = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:b s:b}",
                             "path",
                             &path,
                             "xml",
                             &xml_flag,
                             "force",
                             &force_flag) < 0)
        goto error;
    if (inv->ctx->rank != 0) {
        errno = ENOSYS;
        errstr = "resource.reload is only available on rank 0";
        goto error;
    }
    if (xml_flag) {
        if (!(xml = rutil_load_xml_dir (path, &error))) {
            errstr = error.text;
            goto error;
        }

        if (!(resobj = resobj_from_xml (xml))) {
            errprintf (&error,
                       "error building R from hwloc XML: %s",
                       strerror (errno));
            errstr = error.text;
            goto error;
        }
    }
    else {
        if (!(resobj = rutil_load_file (path, &error))) {
            errstr = error.text;
            goto error;
        }
    }
    if (resobj_check_ranks (resobj, inv->ctx->size) < 0) {
        if (force_flag) {
            flux_log (inv->ctx->h,
                      LOG_ERR,
                      "WARN: resource object contains ranks exceeding size=%d",
                      (int)inv->ctx->size);
        }
        else {
            errprintf (&error,
                       "resource object contains ranks execeeding size=%d %s",
                       (int)inv->ctx->size,
                       "(override with -f))");
            errstr = error.text;
            errno = EINVAL;
            goto error;
        }
    }
    if (acquire_clients (inv->ctx->acquire) > 0) {
        errno = EBUSY;
        errstr = "resources are busy (unload scheduler?)";
        goto error;
    }
    if (inv->R) {
        json_decref (inv->R);
        inv->R = NULL;
        free (inv->method);
        inv->method = NULL;
    }
    if (inventory_put (inv, resobj, "reload") < 0)
        goto error;
    if (inventory_put_finalize (inv) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to resource.reload request");
    json_decref (resobj);
    json_decref (xml);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to resource.reload request");
    json_decref (resobj);
    json_decref (xml);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "resource.reload",  resource_reload, 0 },
    { FLUX_MSGTYPE_REQUEST, "resource.get",  resource_get, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void inventory_destroy (struct inventory *inv)
{
    if (inv) {
        int saved_errno = errno;
        flux_msg_handler_delvec (inv->handlers);
        json_decref (inv->R);
        free (inv->method);
        flux_future_destroy (inv->f);
        free (inv);
        errno = saved_errno;
    }
}

struct inventory *inventory_create (struct resource_ctx *ctx, json_t *conf_R)
{
    struct inventory *inv;
    json_t *R = NULL;

    if (!(inv = calloc (1, sizeof (*inv))))
        return NULL;
    inv->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, inv, &inv->handlers) < 0)
        goto error;
    if (conf_R && convert_R_conf (ctx->h, conf_R, &R) < 0)
        goto error;
    if (ctx->rank == 0) {
        if (R && inventory_put (inv, R, "configuration") < 0)
            goto error;
        if (!inv->R && get_from_kvs (inv, "resource.R") < 0)
            goto error;
        if (!inv->R && get_from_job_info (inv, "R") < 0)
            goto error;
    }
    else {
        if (R)
            inv->R = json_incref (R);
        if (!inv->R && get_from_upstream (inv) < 0)
            goto error;
    }
    /* If inv->R is NULL after all that, dynamic discovery occurs.
     */
    json_decref (R);
    return inv;
error:
    ERRNO_SAFE_WRAP (json_decref, R);
    inventory_destroy (inv);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
