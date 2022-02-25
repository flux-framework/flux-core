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
 * Hwloc XML
 * ---------
 * The Fluxion scheduler needs hwloc XML to bootstrap if it does not receive
 * sufficient hierarchical resource data under the opaque scheduling key of R.
 * The 'resource.get-xml' RPC provides a mechanism for Fluxion to fetch the
 * XML as a rank-indexed JSON array.  Regardless of whether resources were
 * obtained using case 1, 2, or 3, the XML is always collected and made
 * available.  If its collection is still in progress when it is requested,
 * the response is delayed until it completes.
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
 * and use them to generate R.  This method also captures the XML and
 * ensures that 'resource.get-xml' is in sync with R.
 *
 * It's also possible to fake resources by placing them in resource.R and
 * then (re-) loading the resource module.  This is how the sharness 'job'
 * personality fakes resources.  However, if this method is used, the hwloc
 * XML is not synced with R.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include <src/common/librlist/rlist.h>
#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"

#include "rutil.h"
#include "resource.h"
#include "reslog.h"
#include "acquire.h"
#include "inventory.h"


struct inventory {
    struct resource_ctx *ctx;

    json_t *R;
    char *method;
    json_t *xml;

    zlist_t *waiters;

    flux_future_t *f;
    flux_msg_handler_t **handlers;
};

static int rank_from_key (const char *key);

static int inventory_put_finalize (struct inventory *inv)
{
    const char *method = flux_future_aux_get (inv->f, "method");
    int rc = -1;

    if (flux_future_get (inv->f, NULL) < 0) {
        flux_log_error (inv->ctx->h, "error commiting R to KVS");
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

static int xml_to_fixed_array_map (unsigned int id, json_t *val, void *arg)
{
    json_t *array = arg;

    if (id >= json_array_size (array)) {
        errno = EINVAL;
        return -1;
    }
    if (json_array_set (array, id, val) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* Convert object with idset keys to fixed size array, which should be easier
 * for end users to handle.  Any unpopulated array slots are set to JSON null.
 */
static json_t *xml_to_fixed_array (json_t *xml, uint32_t size)
{
    json_t *array;
    int i;

    if (!(array = json_array ()))
        goto nomem;
    for (i = 0; i < size; i++) {
        if (json_array_append (array, json_null ()) < 0)
            goto nomem;
    }
    if (rutil_idkey_map (xml, xml_to_fixed_array_map, array) < 0)
        goto error;
    return array;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, array);
    return NULL;
};

int inventory_put_xml (struct inventory *inv, json_t *xml)
{
    flux_t *h = inv->ctx->h;
    const flux_msg_t *msg;

    if (inv->ctx->rank != 0 || !xml) {
        errno = EINVAL;
        return -1;
    }
    if (inv->xml) {
        errno = EEXIST;
        return -1;
    }
    flux_log (inv->ctx->h, LOG_DEBUG, "xml %d ranks in %zu objects",
              rutil_idkey_count (xml), json_object_size (xml));
    inv->xml = json_incref (xml);

    if (zlist_size (inv->waiters) > 0) {
        json_t *array;

        if (!(array = xml_to_fixed_array (inv->xml, inv->ctx->size)))
            return -1;
        while ((msg = zlist_pop (inv->waiters))) {
            if (flux_respond_pack (h, msg, "{s:O}", "xml", array) < 0)
                flux_log_error (h, "error responding to resource.get-xml");
            flux_msg_decref (msg);
        }
        json_decref (array);
    }
    return 0;
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

json_t *inventory_get_xml (struct inventory *inv)
{
    if (!inv->xml) {
        errno = ENOENT;
        return NULL;
    }
    return inv->xml;
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
                                          char *errbuf,
                                          int errsize)
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
            (void) snprintf (errbuf, errsize,
                             "invalid targets: %s",
                             err.text);
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
    *Rp = R;
    return 0;
error:
    rlist_destroy (rl);
    return -1;
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
    int count;

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
    if (rlist_remap (rl) < 0)
        goto error;
    if (!(R = rlist_to_R (rl)))
        goto error;
    *Rp = R;
noconvert:
    idset_destroy (ranks);
    rlist_destroy (rl);
    return 0;
error:
    idset_destroy (ranks);
    rlist_destroy (rl);
    return -1;
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

static int check_broker_quorum (flux_t *h, bool *is_subset)
{
    const char *quorum;
    uint32_t size;
    char buf[32] = "0";

    if (!(quorum = flux_attr_get (h, "broker.quorum"))
        || flux_get_size (h, &size) < 0)
        return -1;
    if (size > 1) {
        if (snprintf (buf,
                      sizeof (buf),
                      "0-%d",
                      (int)size - 1) >= sizeof (buf)) {
            errno = EOVERFLOW;
            return -1;
        }
    }
    if (strcmp (buf, quorum) == 0)
        *is_subset = false;
    else
        *is_subset = true;
    return 0;
}

/* If xml collection is still in progress, this function delays responding
 * until it completes.
 */
static void resource_get_xml (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct inventory *inv = arg;
    const char *errstr = NULL;
    json_t *array = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (inv->ctx->rank != 0) {
        errno = EPROTO;
        errstr = "this RPC only works on rank 0";
        goto error;
    }
    /* xml is still being collected.  If broker.quorum is not the entire
     * instance, then this may never complete, so fail the request in that
     * case; otherwise save the request in inv->waiters for later response.
     */
    if (!inv->xml) {
        bool is_subset;
        if (check_broker_quorum (h, &is_subset) < 0)
            goto error;
        if (is_subset) {
            errno = EINVAL;
            errstr = "Request may block forever. "
                     "If R is incomplete, there may be a config error.";
            goto error;
        }
        if (zlist_append (inv->waiters, (void *)flux_msg_incref (msg)) < 0) {
            flux_msg_decref (msg);
            errno = ENOMEM;
            goto error;
        }
        return; // response deferred
    }
    if (!(array = xml_to_fixed_array (inv->xml, inv->ctx->size)))
        goto error;
    if (flux_respond_pack (h, msg, "{s:O}", "xml", array) < 0)
        flux_log_error (h, "error responding to resource.get-xml");
    json_decref (array);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to resource.get-xml");
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

static void resource_reload (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct inventory *inv = arg;
    char errbuf[256];
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
        if (!(xml = rutil_load_xml_dir (path, errbuf, sizeof (errbuf)))) {
            errstr = errbuf;
            goto error;
        }

        if (!(resobj = resobj_from_xml (xml))) {
            snprintf (errbuf,
                      sizeof (errbuf),
                      "error buiding R from hwloc XML: %s",
                      strerror (errno));
            errstr = errbuf;
            goto error;
        }
    }
    else {
        if (!(resobj = rutil_load_file (path, errbuf, sizeof (errbuf)))) {
            errstr = errbuf;
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
            snprintf (errbuf,
                      sizeof (errbuf),
                      "resource object contains ranks execeeding size=%d %s",
                      (int)inv->ctx->size,
                      "(override with -f))");
            errstr = errbuf;
            errno = EINVAL;
            goto error;
        }
    }
    if (acquire_clients (inv->ctx->acquire) > 0) {
        errno = EBUSY;
        errstr = "resources are busy (unload scheduler?)";
        goto error;
    }
    if (xml) {
        if (inv->xml) {
            json_decref (inv->xml);
            inv->xml = NULL;
        }
        if (inventory_put_xml (inv, xml) < 0)
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
    { FLUX_MSGTYPE_REQUEST, "resource.get-xml",  resource_get_xml, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void inventory_destroy (struct inventory *inv)
{
    if (inv) {
        int saved_errno = errno;
        flux_msg_handler_delvec (inv->handlers);
        json_decref (inv->R);
        json_decref (inv->xml);
        free (inv->method);
        flux_future_destroy (inv->f);
        if (inv->waiters) {
            const flux_msg_t *msg;
            while ((msg = zlist_pop (inv->waiters)))
                flux_msg_decref (msg);
            zlist_destroy (&inv->waiters);
        }
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
    if (!(inv->waiters = zlist_new ()))
        goto error;
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
