/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* bootstrap.c - determine rank, size, and peer endpoints */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/hostlist.h>
#include <flux/taskmap.h>
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "attr.h"
#include "bootstrap.h"

struct bootstrap {
    struct broker *ctx;
    struct upmi *upmi;
    struct bizcache *cache;
    flux_msg_handler_t **handlers;
    bool under_flux;
    bool finalized;
};

/* Ensure attribute 'key' is set with the immutable flag.
 * If unset, set it to 'default_value'.
 */
static int setattr (attr_t *attrs,
                    const char *key,
                    const char *default_value,
                    flux_error_t *errp)
{
    const char *val;

    if (attr_get (attrs, key, &val) < 0) {
        if (attr_add (attrs, key, default_value) < 0) {
            errprintf (errp, "setattr %s: %s", key, strerror (errno));
            return -1;
        }
    }
    return 0;
}

static const char *getattr (attr_t *attrs, const char *key)
{
    const char *val;
    if (attr_get (attrs, key, &val) < 0)
        return NULL;
    return val;
}

static char *lookup (struct upmi *upmi, const char *key)
{
    char *val;
    if (upmi_get (upmi, key, -1, &val, NULL) < 0)
        return NULL;
    return val;
}

/* Set the broker.mapping attribute.  There are four cases:
 * 1. singleton or config file bootstrap:  always 1 broker per node
 * 2. flux.taskmap set in PMI:  pass thru (re-encode to check)
 * 3. PMI_process_mapping set in PMI:  translate to Flux taskmap
 * 4. no value (NULL)
 */
static int setattr_broker_mapping (struct bootstrap *boot, flux_error_t *errp)
{
    flux_error_t error;
    char *val = NULL;
    struct taskmap *map = NULL;

    if (boot->ctx->info.size == 1
        || streq (upmi_describe (boot->upmi), "config")) {
        if (asprintf (&val, "[[0,%d,1,1]]", boot->ctx->info.size) < 0) {
            errprintf (errp, "broker.mapping: %s", strerror (errno));
            return -1;
        }
    }
    else if (boot->under_flux && (val = lookup (boot->upmi, "flux.taskmap"))) {
        if (!(map = taskmap_decode (val, &error))) {
            errprintf (errp, "flux.taskmap: %s", error.text);
            goto error;
        }
        free (val);
        if (!(val = taskmap_encode (map, 0))) {
            errprintf (errp, "flux.taskmap: %s", strerror (errno));
            goto error;
        }
    }
    else if ((val = lookup (boot->upmi, "PMI_process_mapping"))) {
        if (!(map = taskmap_decode (val, &error))) {
            errprintf (errp, "PMI_process_mapping: %s", error.text);
            goto error;
        }
        free (val);
        if (!(val = taskmap_encode (map, 0))) {
            errprintf (errp, "PMI_process_mapping: %s", strerror (errno));
            goto error;
        }
    }
    if (setattr (boot->ctx->attrs, "broker.mapping", val, errp) < 0)
        goto error;
    taskmap_destroy (map);
    free (val);
    return 0;
error:
    taskmap_destroy (map);
    ERRNO_SAFE_WRAP (free, val);
    return -1;
}

/* Initialize some broker attributes using information obtained during
 * bootstrap, such as pre-put values from the PMI KVS.
 */
static int bootstrap_setattrs_early (struct bootstrap *boot,
                                     flux_error_t *errp)
{
    attr_t *attrs = boot->ctx->attrs;

    /* The info->dict exists so that out of tree upmi plugins, such as the
     * one provided by flux-pmix, can set Flux broker attributes as a way
     * of passing information to applications.
     */
    if (boot->ctx->info.dict) {
        const char *key;
        json_t *value;

        json_object_foreach (boot->ctx->info.dict, key, value) {
            if (!json_is_string (value)) {
                errprintf (errp, "info dict key %s is not a string", key);
                return -1;
            }
            if (setattr (attrs, key, json_string_value (value), errp) < 0)
                return -1;
        }
    }

    /* If running under Flux, setattr instance-level from PMI
     * flux.instance-level.  If not running under Flux (key is missing),
     * set it to zero.
     */
    if (boot->under_flux) {
        char *val = lookup (boot->upmi, "flux.instance-level");
        int rc;

        if (!val)
            boot->under_flux = false;
        rc = setattr (attrs, "instance-level", val ? val : "0", errp);
        free (val);
        if (rc < 0)
            return -1;
    }

    /* If running under Flux, setattr jobid to PMI KVS name.
     */
    if (boot->under_flux) {
        if (setattr (attrs, "jobid", boot->ctx->info.name, errp))
            return -1;
    }

    /* If running under Flux, and not already set, setattr tbon.interface-hint
     * from PMI flux.tbon.interface-hint, if available.
     * This is finalized later by the overlay.
     */
    if (boot->under_flux && !getattr (attrs, "tbon.interface-hint")) {
        char *val = lookup (boot->upmi, "flux.tbon-interface-hint");
        int rc;

        if (val) {
            rc = setattr (attrs, "tbon.interface-hint", val, errp);
            free (val);
            if (rc < 0)
                return -1;
        }
    }

    if (setattr_broker_mapping (boot, errp) < 0)
        return -1;

    return 0;
}

const char *bootstrap_method (struct bootstrap *boot)
{
    return boot ? upmi_describe (boot->upmi) : "unknown";
}

static void trace_upmi (void *arg, const char *text)
{
    fprintf (stderr, "bootstrap: %s\n", text);
}

/* Set the hostlist attribute.  There are three cases:
 * 1. set on command line (used in test)
 * 2. singleton
 * 3. use hostnames from business cards (assuming exchange has occurred).
 */
static int setattr_hostlist (struct bootstrap *boot, flux_error_t *errp)
{
    const char *hostlist;
    const struct bizcard *bc;
    struct hostlist *hl = NULL;
    char *hosts = NULL;

    if ((hostlist = getattr (boot->ctx->attrs, "hostlist"))) {
    }
    else if (boot->ctx->info.size == 1) {
        hostlist = boot->ctx->hostname;
    }
    else {
        if (!(hl = hostlist_create ())) {
            errprintf (errp, "hostlist_create: %s", strerror (errno));
            return -1;
        }
        for (int rank = 0; rank < boot->ctx->info.size; rank++) {
            if (bizcache_get (boot->cache, rank, &bc, errp) < 0)
                goto error;
            if (hostlist_append (hl, bizcard_hostname (bc)) < 0) {
                errprintf (errp, "hostlist_append: %s", strerror (errno));
                goto error;
            }
        }
        if (!(hosts = hostlist_encode (hl))) {
            errprintf (errp, "hostlist_encode: %s", strerror (errno));
            goto error;
        }
        hostlist = hosts;
    }
    if (setattr (boot->ctx->attrs, "hostlist", hostlist, errp) < 0)
        goto error;
    free (hosts);
    hostlist_destroy (hl);
    return 0;
error:
    hostlist_destroy (hl);
    ERRNO_SAFE_WRAP (free, hosts);
    return -1;
}

static int setattr_broker_critical_ranks (struct bootstrap *boot,
                                          const char *default_value,
                                          flux_error_t *errp)
{
    const char *crit;
    char *val = NULL;
    int rc;

    if ((crit = getattr (boot->ctx->attrs, "broker.critical-ranks"))) {
        struct idset *ids = idset_decode (crit);
        unsigned int last = idset_last (ids);
        idset_destroy (ids);
        if (last == IDSET_INVALID_ID || last >= boot->ctx->info.size) {
            errprintf (errp,
                       "invalid value for broker.critical-ranks='%s'",
                       crit);
            return -1;
        }
    }
    else {
        if (default_value)
            crit = default_value;
        else {
            if (asprintf (&val, "0-%d", boot->ctx->info.size - 1) < 0) {
                errprintf (errp,
                           "building broker.critical-ranks: %s",
                           strerror (errno));
                return -1;
            }
            crit = val;
        }
    }
    rc = setattr (boot->ctx->attrs, "broker.critical-ranks", crit, errp);
    free (val);
    return rc;
}

/*  Encode idset of critical nodes/shell ranks, which is calculated
 *   from broker.mapping and broker.critical-ranks.
 */
static char *encode_critical_nodes (attr_t *attrs)
{
    struct idset *ranks = NULL;
    struct idset *nodeids = NULL;
    struct taskmap *map = NULL;
    char *s = NULL;
    int nodeid;
    const char *mapping;
    const char *ranks_attr;
    unsigned int i;

    if (!(mapping = getattr (attrs, "broker.mapping"))
        || !(map = taskmap_decode (mapping, NULL))
        || !(ranks_attr = getattr (attrs, "broker.critical-ranks"))
        || !(ranks = idset_decode (ranks_attr))
        || !(nodeids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto done;

    /*  Map the broker ranks from the broker.critical-ranks attr to
     *  shell ranks/nodeids using PMI_process_mapping (this handles the
     *  rare case where multiple brokers per node/shell were launched)
     */
    i = idset_first (ranks);
    while (i != IDSET_INVALID_ID) {
        if ((nodeid = taskmap_nodeid (map, i)) < 0
            || idset_set (nodeids, nodeid) < 0)
            goto done;
        i = idset_next (ranks, i);
    }
    s = idset_encode (nodeids, IDSET_FLAG_RANGE);
done:
    taskmap_destroy (map);
    idset_destroy (ranks);
    idset_destroy (nodeids);
    return s;
}

static flux_future_t *set_critical_ranks (flux_t *h,
                                          flux_jobid_t id,
                                          attr_t *attrs)
{
    int saved_errno;
    flux_future_t *f;
    char *nodeids;

    if (!(nodeids = encode_critical_nodes (attrs)))
        return NULL;
    f = flux_rpc_pack (h,
                       "job-exec.critical-ranks",
                       FLUX_NODEID_ANY, 0,
                       "{s:I s:s}",
                       "id", id,
                       "ranks", nodeids);
    saved_errno = errno;
    free (nodeids);
    errno = saved_errno;
    return f;
}

static flux_future_t *set_uri_job_memo (flux_t *h,
                                        const char *hostname,
                                        flux_jobid_t id,
                                        attr_t *attrs,
                                        flux_error_t *errp)
{
    const char *local_uri = NULL;
    const char *path;
    char uri [1024];
    flux_future_t *f;

    if (!(local_uri = getattr (attrs, "local-uri"))) {
        errprintf (errp, "Unexpectedly unable to fetch local-uri attribute");
        return NULL;
    }
    path = local_uri + 8; /* forward past "local://" */
    if (snprintf (uri,
                 sizeof (uri),
                 "ssh://%s%s",
                 hostname, path) >= sizeof (uri)) {
        errprintf (errp, "buffer overflow while checking local-uri");
        return NULL;
    }
    if (!(f = flux_rpc_pack (h,
                             "job-manager.memo",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:{s:s}}",
                             "id", id,
                             "memo",
                             "uri", uri))) {
        errprintf (errp,
                   "error sending job-manager.memo request: %s",
                   strerror (errno));
        return NULL;
    }
    return f;
}

static int execute_parental_notifications (struct bootstrap *boot,
                                           flux_error_t *errp)
{
    const char *jobid = NULL;
    const char *parent_uri = NULL;
    flux_jobid_t id;
    flux_t *h = NULL;
    flux_future_t *f = NULL;
    flux_future_t *f2 = NULL;
    int rc = -1;

    /*  This is rank 0 of an instance started by Flux.
     */
    if (!(parent_uri = getattr (boot->ctx->attrs, "parent-uri")))
        return errprintf (errp, "getattr parent-uri failed");
    if (!(jobid = getattr (boot->ctx->attrs, "jobid")))
        return errprintf (errp, "getattr jobid failed");

    if (flux_job_id_parse (jobid, &id) < 0)
        return errprintf (errp, "Unable to parse jobid attribute '%s'", jobid);

    /*  Open connection to parent instance:
     */
    if (!(h = flux_open (parent_uri, 0))) {
        return errprintf (errp,
                          "flux_open to parent failed %s",
                          strerror (errno));
    }

    /*  Perform any RPCs to parent in parallel */
    if (!(f = set_uri_job_memo (h,
                                boot->ctx->hostname,
                                id,
                                boot->ctx->attrs,
                                errp)))
        goto out;

    /*  Note: not an error if rpc to set critical ranks fails, but
     *  issue an error notifying user that no critical ranks are set.
     */
    if (!(f2 = set_critical_ranks (h, id, boot->ctx->attrs))) {
        flux_log (boot->ctx->h,
                  LOG_ERR,
                  "Unable to get critical ranks, all ranks will be critical");
    }

    /*  Wait for RPC results */
    if (flux_future_get (f, NULL) < 0) {
        errprintf (errp,
                   "job-manager.memo uri: %s",
                   future_strerror (f, errno));
        goto out;
    }
    if (f2 && flux_future_get (f2, NULL) < 0 && errno != ENOSYS) {
        errprintf (errp,
                   "job-exec.critical-ranks: %s",
                   future_strerror (f2, errno));
        goto out;
    }
    rc = 0;
out:
    flux_close (h);
    flux_future_destroy (f);
    flux_future_destroy (f2);
    return rc;
}

int bootstrap_finalize (struct bootstrap *boot,
                        const char *default_critical_ranks,
                        flux_error_t *errp)
{
    flux_error_t error;

    if (boot->finalized)
        return 0;
    if (setattr_hostlist (boot, errp) < 0)
        return -1;
    if (setattr_broker_critical_ranks (boot, default_critical_ranks, errp) < 0)
        return -1;
    if (boot->under_flux && boot->ctx->info.rank == 0) {
        if (execute_parental_notifications (boot, errp) < 0)
            return -1;
    }
    if (attr_cache_immutables (boot->ctx->attrs, boot->ctx->h) < 0) {
        errprintf (errp, "error caching immutables");
        return -1;
    }
    if (upmi_finalize (boot->upmi, &error) < 0) {
        errprintf (errp,
                   "%s: finalize: %s",
                   upmi_describe (boot->upmi),
                   error.text);
        return -1;
    }
    boot->finalized = true;

    return 0;
}

/* Overlay calls bootstrap.iam to publish this broker's business card.
 */
static void bootstrap_iam_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct bootstrap *boot = arg;
    json_t *bizcard;
    struct bizcard *bc = NULL;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:o}", "bizcard", &bizcard) < 0)
        goto error;
    if (!(bc = bizcard_fromjson (bizcard))) {
        errprintf (&error, "bizcard decode error: %s", strerror (errno));
        errmsg = error.text;
        goto error;
    }
    if (bizcache_put (boot->cache, boot->ctx->info.rank, bc, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to bootstrap.iam request");
    bizcard_decref (bc);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to bootstrap.iam request");
    bizcard_decref (bc);
}

/* Overlay calls bootstrap.barrier after putting its own business card.
 * It may then start fetching peer business cards.
 */
static void bootstrap_barrier_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct bootstrap *boot = arg;
    flux_error_t e;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (upmi_barrier (boot->upmi, &e) < 0) {
        errprintf (&error,
                   "%s: barrier: %s",
                   upmi_describe (boot->upmi),
                   e.text);
        errmsg = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to bootstrap.barrier request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to bootstrap.barrier request");
}

/* Overlay calls bootstrap.whois to fetch the business cards of its
 * direct peers, by rank.  The business cards are streamed, one per
 * response.  This may be called multiple times.
 */
static void bootstrap_whois_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct bootstrap *boot = arg;
    json_t *ranks;
    struct idset *ids = NULL;
    unsigned long rank;
    flux_error_t error;
    flux_error_t e;
    const char *errmsg = NULL;
    const struct bizcard *bc;

    if (flux_request_unpack (msg, NULL, "{s:o}", "ranks", &ranks) < 0)
        goto error;
    if (json_is_integer (ranks)) {
        if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW))
            || idset_set (ids, json_integer_value (ranks)) < 0)
            goto error;
    }
    else if (json_is_string (ranks)) {
        if (!(ids = idset_decode (json_string_value (ranks))))
            goto error;
    }
    else
        goto error_proto;
    if (idset_count (ids) > 1 && !flux_msg_is_streaming (msg))
        goto error_proto;
    rank = idset_first (ids);
    while (rank != IDSET_INVALID_ID) {
        if (bizcache_get (boot->cache, rank, &bc, &e) < 0) {
            errprintf (&error,
                       "error fetching bizcard for rank %lu: %s",
                       rank,
                       e.text);
            errmsg = error.text;
            goto error;
        }
        if (flux_respond_pack (h,
                               msg,
                               "{s:i s:O}",
                               "rank", rank,
                               "bizcard", bizcard_get_json (bc)) < 0)
            flux_log_error (h, "error responding to bootstrap.whois request");
        rank = idset_next (ids, rank);
    }
    if (flux_msg_is_streaming (msg)) {
        if (flux_respond_error (h, msg, ENODATA, NULL) < 0)
            flux_log_error (h, "error responding to bootstrap.whois request");
    }
    idset_destroy (ids);
    return;
error_proto:
    errno = EPROTO;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to bootstrap.whois request");
    idset_destroy (ids);
}

/* overlay calls bootstrap.finalize to end the bootstrap session.
 * The overlay also specifies a topology-aware set of default critical ranks.
 */
static void bootstrap_finalize_cb (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct bootstrap *boot = arg;
    const char *crit = NULL;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s?s}", "crit", &crit) < 0)
        goto error;
    if (bootstrap_finalize (boot, crit, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to bootstrap.finalize request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to bootstrap.finalize request");
}


static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "bootstrap.iam",
        bootstrap_iam_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "bootstrap.whois",
        bootstrap_whois_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "bootstrap.barrier",
        bootstrap_barrier_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "bootstrap.finalize",
        bootstrap_finalize_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void bootstrap_destroy (struct bootstrap *boot)
{
    if (boot) {
        int saved_errno = errno;
        flux_msg_handler_delvec (boot->handlers);
        bizcache_destroy (boot->cache);
        upmi_destroy (boot->upmi);
        free (boot);
        errno = saved_errno;
    }
}

struct bootstrap *bootstrap_create (struct broker *ctx,
                                    struct upmi_info *info,
                                    flux_error_t *errp)
{
    struct bootstrap *boot;
    const char *upmi_method = NULL;
    int upmi_flags = UPMI_LIBPMI_NOFLUX;
    json_t *upmi_args = NULL;
    json_t *conf_obj;
    flux_error_t error;

    if (!(boot = calloc (1, sizeof (*boot)))) {
        errprintf (errp, "out of memory");
        return NULL;
    }
    boot->ctx = ctx;

    if (getenv ("FLUX_PMI_DEBUG"))
        upmi_flags |= UPMI_TRACE;

    if (flux_conf_unpack (flux_get_conf (ctx->h), NULL, "o", &conf_obj) < 0
        || !(upmi_args = json_pack ("{s:O s:s}",
                                    "config", conf_obj,
                                    "hostname", ctx->hostname))) {
        errprintf (errp, "error preparing upmi_args");
        goto error;
    }
    (void)attr_get (ctx->attrs, "broker.boot-method", &upmi_method);
    if (!(boot->upmi = upmi_create_ex (upmi_method,
                                       upmi_flags,
                                       upmi_args,
                                       trace_upmi,
                                       NULL,
                                       errp)))
        goto error;
    if (setattr (ctx->attrs,
                 "broker.boot-method",
                 upmi_describe (boot->upmi),
                 errp) < 0)
        goto error;

    if (upmi_initialize (boot->upmi, info, &error) < 0) {
        errprintf (errp,
                   "%s: initialize: %s",
                   upmi_describe (boot->upmi),
                   error.text);
        goto error;
    }
    boot->under_flux = true; // until proven otherwise
    if (boot->ctx->verbose) {
        flux_log (boot->ctx->h,
                  LOG_INFO,
                  "boot: rank=%d size=%d",
                  info->rank,
                  info->size);
    }
    if (bootstrap_setattrs_early (boot, &error) < 0) {
        errprintf (errp, "%s: %s", upmi_describe (boot->upmi), error.text);
        goto error;
    }
    if (!(boot->cache = bizcache_create (boot->upmi, info->size))) {
        errprintf (errp,
                   "%s: error creating business card cache: %s",
                   upmi_describe (boot->upmi),
                   strerror (errno));
        goto error;
    }
    if (flux_msg_handler_addvec (boot->ctx->h,
                                 htab,
                                 boot,
                                 &boot->handlers) < 0) {
        errprintf (errp,
                   "%s: error registering message handlers: %s",
                   upmi_describe (boot->upmi),
                   strerror (errno));
        goto error;
    }

    json_decref (upmi_args);
    return boot;
error:
    ERRNO_SAFE_WRAP (json_decref, upmi_args);
    bootstrap_destroy (boot);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
