/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <inttypes.h>
#include <flux/core.h>

#include "jstatctl.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/lru_cache.h"
#include "shortjansson.h"


/*******************************************************************************
 *                                                                             *
 *                      Internal User Define Types and Data                    *
 *                                                                             *
 *******************************************************************************/

typedef struct {
    job_state_t i;
    const char *s;
} stab_t;

typedef struct {
   jsc_handler_f cb;
   void *arg;
} cb_pair_t;

typedef struct {
    zhash_t *active_jobs;
    lru_cache_t *kvs_paths;
    flux_msg_handler_t **handlers;
    zlist_t *callbacks;
    flux_t *h;
} jscctx_t;

static stab_t job_state_tab[] = {
    { J_NULL,       "null" },
    { J_RESERVED,   "reserved" },
    { J_SUBMITTED,  "submitted" },
    { J_PENDING,    "pending" },
    { J_SCHEDREQ,   "schedreq" },
    { J_SELECTED,   "selected" },
    { J_ALLOCATED,  "allocated" },
    { J_RUNREQUEST, "runrequest" },
    { J_STARTING,   "starting" },
    { J_SYNC,       "sync" },
    { J_RUNNING,    "running" },
    { J_CANCELLED,  "cancelled" },
    { J_COMPLETING, "completing" },
    { J_COMPLETE,   "complete" },
    { J_REAPED,     "reaped" },
    { J_FAILED,     "failed" },
    { J_FOR_RENT,   "for_rent" },
    { -1, NULL },
};


/******************************************************************************
 *                                                                            *
 *                              Utilities                                     *
 *                                                                            *
 ******************************************************************************/

const char *jsc_job_num2state (job_state_t i)
{
    stab_t *ss = job_state_tab;
    while (ss->s != NULL) {
        if (ss->i == i)
            return ss->s;
        ss++;
    }
    return NULL;
}

int jsc_job_state2num (const char *s)
{
    stab_t *ss = job_state_tab;
    while (ss->s != NULL) {
        if (!strcmp (ss->s, s))
            return ss->i;
        ss++;
    }
    return -1;
}

static void freectx (void *arg)
{
    jscctx_t *ctx = arg;
    zhash_destroy (&(ctx->active_jobs));
    lru_cache_destroy (ctx->kvs_paths);
    zlist_destroy (&(ctx->callbacks));
    flux_msg_handler_delvec (ctx->handlers);
    free (ctx);
}

static jscctx_t *getctx (flux_t *h)
{
    jscctx_t *ctx = (jscctx_t *)flux_aux_get (h, "jstatctrl");
    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->active_jobs = zhash_new ()))
            oom ();
        if (!(ctx->kvs_paths = lru_cache_create (256)))
            oom ();
        lru_cache_set_free_f (ctx->kvs_paths, free);
        if (!(ctx->callbacks = zlist_new ()))
            oom ();
        ctx->h = h;
        flux_aux_set (h, "jstatctrl", ctx, freectx);
    }
    return ctx;
}

static int lwj_kvs_path (flux_t *h, int64_t id, char **pathp)
{
    int rc = -1;
    const char *path;
    flux_future_t *f;
    uint32_t nodeid = FLUX_NODEID_ANY;

    if (!(f = flux_rpc_pack (h, "job.kvspath", nodeid, 0, "{s:[I]}", "ids", id))
        ||  flux_rpc_get_unpack (f, "{s:[s]}", "paths", &path) < 0) {
        flux_log_error (h, "flux_rpc (job.kvspath)");
        goto out;
    }
    if (!(*pathp = strdup (path))) {
        flux_log_error (h, "flux_rpc (job.kvspath): strdup");
        goto out;
    }
    rc = 0;
out:
    flux_future_destroy (f);
    return (rc);
}

static int jscctx_add_jobid_path (jscctx_t *ctx, int64_t id, const char *path)
{
    char *s;
    char key [21];
    memset (key, 0, sizeof (key));
    if (sprintf (key, "%"PRId64, id) < 0)
        return (-1);
    if (!(s = strdup (path)))
        return (-1);
    if (lru_cache_put (ctx->kvs_paths, key, s) < 0) {
        if (errno != EEXIST)
            flux_log_error (ctx->h, "jscctx_add_job_path");
        free (s);
    }
    return (0);
}

static const char * jscctx_jobid_path (jscctx_t *ctx, int64_t id)
{
    char *path;
    char key [21];

    memset (key, 0, sizeof (key));
    if (sprintf (key, "%"PRId64, id) < 0)
        return (NULL);
    if ((path = lru_cache_get (ctx->kvs_paths, key)))
        return path;
    if (lwj_kvs_path (ctx->h, id, &path) < 0)
        return (NULL);
    if (lru_cache_put (ctx->kvs_paths, key, path) < 0)
        return (NULL);
    return (path);
}

static int fetch_and_update_state (zhash_t *aj , int64_t j, int64_t ns)
{
    int *t = NULL;
    char *key = NULL;

    if (!aj) return J_FOR_RENT;
    key = xasprintf ("%"PRId64, j);
    if ( !(t = ((int *)zhash_lookup (aj, (const char *)key)))) {
        free (key);
        return J_FOR_RENT;
    }
    if (ns == J_COMPLETE || ns == J_FAILED)
        zhash_delete (aj, key);
    else
        zhash_update (aj, key, (void *)(intptr_t)ns);
    free (key);

    /* safe to convert t to int */
    return (intptr_t) t;
}

/******************************************************************************
 *                                                                            *
 *                         Internal JCB Accessors                             *
 *                                                                            *
 ******************************************************************************/

static int jobid_exist (flux_t *h, int64_t j)
{
    flux_future_t *f = NULL;
    jscctx_t *ctx = getctx (h);
    const char *path = jscctx_jobid_path (ctx, j);
    int rc = -1;

    if (path == NULL)
        goto done;
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, path))
                                || flux_kvs_lookup_get_dir (f, NULL) < 0) {
        flux_log (h, LOG_DEBUG, "flux_kvs_lookup(%s): %s",
                     path, flux_strerror (errno));
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static bool fetch_rank_pdesc (json_t *src, int64_t *p, int64_t *n,
			      const char **c)
{
    if (!src) return false;
    if (!Jget_str (src, "command", c)) return false;
    if (!Jget_int64 (src, "pid", p)) return false;
    if (!Jget_int64 (src, "nodeid", n)) return false;
    return true;
}

static int build_name_array (zhash_t *ha, const char *k, json_t *ns)
{
    int i = (intptr_t) zhash_lookup (ha, k);
    if ((void *)((intptr_t)i) == NULL) {
        json_t *k_obj;
        if (!(k_obj = json_string (k)))
            oom ();
        i = json_array_size (ns);
        if (json_array_append_new (ns, k_obj) < 0)
            oom ();
        zhash_insert (ha, k, (void *)(intptr_t)i+1);
    } else
        i--;
    return i;
}

static char * lwj_key (flux_t *h, int64_t id, const char *fmt, ...)
{
    int rc;
    va_list ap;
    jscctx_t *ctx = getctx (h);
    const char *base;
    char *p = NULL;
    char *key;
    if (!(base = jscctx_jobid_path (ctx, id)))
        return (NULL);
    if (fmt != NULL) {
        va_start (ap, fmt);
        rc = vasprintf (&p, fmt, ap);
        va_end (ap);
        if (rc < 0)
            return (NULL);
    }
    if (asprintf (&key, "%s%s", base, p) < 0)
        return (NULL);
    free (p);
    return (key);
}

static int extract_raw_ngpus (flux_t *h, int64_t j, int64_t *ngpus)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".ngpus");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "I", ngpus) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *ngpus);
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_nnodes (flux_t *h, int64_t j, int64_t *nnodes)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".nnodes");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "I", nnodes) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *nnodes);
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_ntasks (flux_t *h, int64_t j, int64_t *ntasks)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".ntasks");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "I", ntasks) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *ntasks);
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_ncores (flux_t *h, int64_t j, int64_t *ncores)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".ncores");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
            || flux_kvs_lookup_get_unpack (f, "I", ncores) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *ncores);
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_walltime (flux_t *h, int64_t j, int64_t *walltime)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".walltime");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "I", walltime) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *walltime);
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_rdl (flux_t *h, int64_t j, char **rdlstr)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".rdl");
    const char *s;
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get (f, &s) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else {
        *rdlstr = xstrdup (s);
        flux_log (h, LOG_DEBUG, "rdl under %s extracted", key);
    }
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_r_lite (flux_t *h, int64_t j, char **rlitestr)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".R_lite");
    const char *s;
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get (f, &s) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else {
        *rlitestr = xstrdup (s);
        flux_log (h, LOG_DEBUG, "R_lite under %s extracted", key);
    }
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_state (flux_t *h, int64_t j, int64_t *s)
{
    int rc = 0;
    char *key = lwj_key (h, j, ".state");
    const char *state;
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "s", &state) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else {
        *s = jsc_job_state2num (state);
        flux_log (h, LOG_DEBUG, "extract %s: %s", key, state);
    }
    free (key);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_pdesc (flux_t *h, int64_t j, int64_t i, json_t **o)
{
    flux_future_t *f = NULL;
    const char *json_str;
    char *key = lwj_key (h, j, ".%ju.procdesc", i);
    int rc = -1;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get (f, &json_str) < 0
             || !(*o = Jfromstr (json_str))) {
        flux_log_error (h, "extract %s", key);
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    free (key);
    return rc;
}

static json_t *build_parray_elem (int64_t pid, int64_t eix, int64_t hix)
{
    json_t *po = Jnew ();
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_PID, pid);
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_EINDX, eix);
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_HINDX, hix);
    return po;
}

static void add_pdescs_to_jcb (json_t **hns, json_t **ens,
			       json_t **pa, json_t *jcb)
{
    if (json_object_set_new (jcb, JSC_PDESC_HOSTNAMES, *hns) < 0)
        oom ();
    if (json_object_set_new (jcb, JSC_PDESC_EXECS, *ens) < 0)
        oom ();
    if (json_object_set_new (jcb, JSC_PDESC_PDARRAY, *pa) < 0)
        oom ();
    /* Because the above transfer ownership, assign NULL should be ok */
    *hns = NULL;
    *ens = NULL;
    *pa = NULL;
}

static int extract_raw_pdescs (flux_t *h, int64_t j, int64_t n, json_t *jcb)
{
    int rc = -1;
    int64_t i = 0;
    char *hnm;
    const char *cmd = NULL;
    zhash_t *eh = NULL; /* hash holding a set of unique exec_names */
    zhash_t *hh = NULL; /* hash holding a set of unique host_names */
    json_t *o = NULL;
    json_t *po = NULL;
    json_t *pa = Jnew_ar ();
    json_t *hns = Jnew_ar ();
    json_t *ens = Jnew_ar ();

    if (!(eh = zhash_new ()) || !(hh = zhash_new ()))
        oom ();
    for (i=0; i < (int) n; i++) {
        int64_t eix = 0, hix = 0;
        int64_t pid = 0, nid = 0;

        if (extract_raw_pdesc (h, j, i, &o) != 0)
            goto done;
        if (!fetch_rank_pdesc (o, &pid, &nid, &cmd))
            goto done;

        eix = build_name_array (eh, cmd, ens);
        /* FIXME: we need a hostname service */
        hnm = xasprintf ("%"PRId64, nid);
        hix = build_name_array (hh, hnm, hns);
        po = build_parray_elem (pid, eix, hix);
        if (json_array_append_new (pa, po) < 0)
            oom ();
        po = NULL;
        Jput (o);
        o = NULL;
        free (hnm);
    }
    add_pdescs_to_jcb (&hns, &ens, &pa, jcb);
    rc = 0;

done:
    if (o) Jput (o);
    if (po) Jput (po);
    if (pa) Jput (pa);
    if (hns) Jput (hns);
    if (ens) Jput (ens);
    zhash_destroy (&eh);
    zhash_destroy (&hh);
    return rc;
}

static int query_jobid (flux_t *h, int64_t j, json_t **jcb)
{
    int rc = 0;
    if ( ( rc = jobid_exist (h, j)) != 0)
        *jcb = NULL;
    else {
        *jcb = Jnew ();
        Jadd_int64 (*jcb, JSC_JOBID, j);
    }
    return rc;
}

static int query_state_pair (flux_t *h, int64_t j, json_t **jcb)
{
    json_t *o = NULL;
    int64_t st = (int64_t)J_FOR_RENT;;

    if (extract_raw_state (h, j, &st) < 0) return -1;

    *jcb = Jnew ();
    o = Jnew ();
    /* Old state is unavailable through the query.
     * One should use notification service instead.
     */
    Jadd_int64 (o, JSC_STATE_PAIR_OSTATE, st);
    Jadd_int64 (o, JSC_STATE_PAIR_NSTATE, st);
    if (json_object_set_new (*jcb, JSC_STATE_PAIR, o) < 0)
        oom ();
    return 0;
}

static int query_rdesc (flux_t *h, int64_t j, json_t **jcb)
{
    json_t *o = NULL;
    int64_t nnodes = -1;
    int64_t ntasks = -1;
    int64_t ncores = -1;
    int64_t ngpus = 0;
    int64_t walltime = -1;

    if (extract_raw_nnodes (h, j, &nnodes) < 0) return -1;
    if (extract_raw_ntasks (h, j, &ntasks) < 0) return -1;
    if (extract_raw_ncores (h, j, &ncores) < 0) return -1;
    if (extract_raw_walltime (h, j, &walltime) < 0) return -1;
    if (extract_raw_ngpus (h, j, &ngpus) < 0) return -1;

    *jcb = Jnew ();
    o = Jnew ();
    Jadd_int64 (o, JSC_RDESC_NNODES, nnodes);
    Jadd_int64 (o, JSC_RDESC_NTASKS, ntasks);
    Jadd_int64 (o, JSC_RDESC_NCORES, ncores);
    Jadd_int64 (o, JSC_RDESC_WALLTIME, walltime);
    Jadd_int64 (o, JSC_RDESC_NGPUS, ngpus);
    if (json_object_set_new (*jcb, JSC_RDESC, o) < 0)
        oom ();
    return 0;
}

int jsc_query_rdesc_efficiently (flux_t *h, int64_t jobid,
                                 int64_t *nnodes, int64_t *ntasks,
                                 int64_t *ncores, int64_t *walltime)
{
    if (nnodes) {
        if (extract_raw_nnodes (h, jobid, nnodes) < 0)
            return -1;
    }
    if (ntasks) {
        if (extract_raw_ntasks (h, jobid, ntasks) < 0)
            return -1;
    }
    if (ncores) {
        if (extract_raw_ncores (h, jobid, ncores) < 0)
            return -1;
    }
    if (walltime) {
        if (extract_raw_walltime (h, jobid, walltime) < 0)
            return -1;
    }
    return 0;
}

static int query_rdl (flux_t *h, int64_t j, json_t **jcb)
{
    char *rdlstr = NULL;

    if (extract_raw_rdl (h, j, &rdlstr) < 0) return -1;

    *jcb = Jnew ();
    Jadd_str (*jcb, JSC_RDL, (const char *)rdlstr);
    /* Note: seems there is no mechanism to transfer ownership
     * of this string to jcb */
    if (rdlstr)
        free (rdlstr);
    return 0;
}

static int query_r_lite (flux_t *h, int64_t j, json_t **jcb)
{
    char *rlitestr = NULL;

    if (extract_raw_r_lite (h, j, &rlitestr) < 0) return -1;

    *jcb = Jnew ();
    Jadd_str (*jcb, JSC_R_LITE, (const char *)rlitestr);
    /* Note: seems there is no mechanism to transfer ownership
     * of this string to jcb */
    if (rlitestr)
        free (rlitestr);
    return 0;
}

static int query_pdesc (flux_t *h, int64_t j, json_t **jcb)
{
    int64_t ntasks = 0;
    if (extract_raw_ntasks (h, j, &ntasks) < 0) return -1;
    *jcb = Jnew ();
    Jadd_int64 (*jcb, JSC_PDESC_SIZE, ntasks);
    return extract_raw_pdescs (h, j, ntasks, *jcb);
}

static int send_state_event (flux_t *h, job_state_t st, int64_t j)
{
    flux_msg_t *msg;
    char *json = NULL;
    char *topic = NULL;
    int rc = -1;

    if (asprintf (&topic, "jsc.state.%s", jsc_job_num2state (st)) < 0) {
        errno = ENOMEM;
        flux_log_error (h, "create state change event: %s",
                        jsc_job_num2state (st));
        goto done;
    }
    if ((msg = flux_event_pack (topic, "{ s:I }", "jobid", j)) == NULL) {
        flux_log_error (h, "flux_event_pack");
        goto done;
    }
    if (flux_send (h, msg, 0) < 0)
        flux_log_error (h, "flux_send event");
    flux_msg_destroy (msg);
    rc = 0;
done:
    free (topic);
    free (json);
    return rc;
}

static int update_state (flux_t *h, int64_t j, json_t *o)
{
    int rc = -1;
    int64_t st = 0;
    char *key = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!Jget_int64 (o, JSC_STATE_PAIR_NSTATE, &st))
        goto done;
    if ((st >= J_FOR_RENT) || (st < J_NULL))
        goto done;
    if (!(key = lwj_key (h, j, ".state")))
        goto done;
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key, "s",
                           jsc_job_num2state ((job_state_t)st)) < 0) {
        flux_log_error (h, "update %s", key);
        goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit %s", key);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new state: %s", j,
          jsc_job_num2state ((job_state_t)st));
    rc = 0;

    if (send_state_event (h, st, j) < 0)
        flux_log_error (h, "send state event");

done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (key);
    return rc;
}

static int update_rdesc (flux_t *h, int64_t j, json_t *o)
{
    int rc = -1;
    int64_t nnodes = 0;
    int64_t ntasks = 0;
    int64_t ncores = 0;
    int64_t walltime = 0;
    char *key1 = NULL;
    char *key2 = NULL;
    char *key3 = NULL;
    char *key4 = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!Jget_int64 (o, JSC_RDESC_NNODES, &nnodes))
        goto done;
    if (!Jget_int64 (o, JSC_RDESC_NTASKS, &ntasks))
        goto done;
    if (!Jget_int64 (o, JSC_RDESC_NCORES, &ncores))
        goto done;
    if (!Jget_int64 (o, JSC_RDESC_WALLTIME, &walltime))
        goto done;
    if ((nnodes < 0) || (ntasks < 0) || (ncores < 0) || (walltime < 0))
        goto done;
    key1 = lwj_key (h, j, ".nnodes");
    key2 = lwj_key (h, j, ".ntasks");
    key3 = lwj_key (h, j, ".walltime");
    key4 = lwj_key (h, j, ".ncores");
    if (!key1 || !key2 || !key3 || !key4)
        goto done;
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key1, "I", nnodes) < 0) {
        flux_log_error (h, "update %s", key1);
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key2, "I", ntasks) < 0) {
        flux_log_error (h, "update %s", key2);
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key3, "I", walltime) < 0) {
        flux_log_error (h, "update %s", key3);
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key4, "I", ncores) < 0) {
        flux_log_error (h, "update %s", key4);
        goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit failed");
        goto done;
    }
    flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new resources.", j);
    rc = 0;
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (key1);
    free (key2);
    free (key3);
    free (key4);

    return rc;
}

static int update_rdl (flux_t *h, int64_t j, const json_t *rdl)
{
    int rc = -1;
    char *key = lwj_key (h, j, ".rdl");
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key, "O", rdl) < 0) {
        flux_log_error (h, "update %s", key);
        goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit failed");
        goto done;
    }
    flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new rdl.", j);
    rc = 0;

done:
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    free (key);

    return rc;
}

static int update_r_lite (flux_t *h, int64_t j, const json_t *r_lite)
{
    int rc = -1;
    char *key = lwj_key (h, j, ".R_lite");
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key, "O", r_lite) < 0) {
        flux_log_error (h, "update %s", key);
        goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit failed");
        goto done;
    }
    flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new R_lite.", j);
    rc = 0;

done:
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    free (key);

    return rc;
}

static int update_1pdesc (flux_t *h, flux_kvs_txn_t *txn,
              int r, int64_t j, json_t *o,
			  json_t *ha, json_t *ea)
{
    flux_future_t *f = NULL;
    int rc = -1;
    json_t *d = NULL;
    char *key;
    const char *json_str;
    const char *hn = NULL, *en = NULL;
    int64_t pid = 0, hindx = 0, eindx = 0, hrank = 0;
    char *d_str = NULL;

    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_PID, &pid)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_HINDX, &hindx)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_EINDX, &eindx)) return -1;
    if (!Jget_ar_str (ha, (int)hindx, &hn)) return -1;
    if (!Jget_ar_str (ea, (int)eindx, &en)) return -1;

    key = lwj_key (h, j, ".%d.procdesc", r);

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get (f, &json_str) < 0
            || !(d = Jfromstr (json_str))) {
        flux_log_error (h, "extract %s", key);
        goto done;
    }

    Jadd_str (d, "command", en);
    Jadd_int64 (d, "pid", pid);
    errno = 0;
    if ( (hrank = strtoul (hn, NULL, 10)) && errno != 0) {
        flux_log (h, LOG_ERR, "invalid hostname %s", hn);
        goto done;
    }
    Jadd_int64 (d, "nodeid", (int64_t)hrank);
    if (!(d_str = json_dumps (d, 0)))
        oom ();
    if (flux_kvs_txn_put (txn, 0, key, d_str) < 0) {
        flux_log_error (h, "put %s", key);
        goto done;
    }
    rc = 0;

done:
    flux_future_destroy (f);
    free (key);
    if (d)
        Jput (d);
    free (d_str);
    return rc;
}

static int update_pdesc (flux_t *h, int64_t j, json_t *o)
{
    int i = 0;
    int rc = -1;
    int64_t size = 0;
    json_t *h_arr = NULL;
    json_t *e_arr = NULL;
    json_t *pd_arr = NULL;
    json_t *pde = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!Jget_int64 (o, JSC_PDESC_SIZE, &size))
        goto done;
    if (!Jget_obj (o, JSC_PDESC_PDARRAY, &pd_arr))
        goto done;
    if (!Jget_obj (o, JSC_PDESC_HOSTNAMES, &h_arr))
        goto done;
    if (!Jget_obj (o, JSC_PDESC_EXECS, &e_arr))
        goto done;
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    for (i=0; i < (int) size; ++i) {
        if (!Jget_ar_obj (pd_arr, i, &pde))
            goto done;
        if ( (rc = update_1pdesc (h, txn, i, j, pde, h_arr, e_arr)) < 0)
            goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log (h, LOG_ERR, "update_pdesc commit failed");
        goto done;
    }
    rc = 0;

done:
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    return rc;
}

static json_t *get_update_jcb (flux_t *h, int64_t jobid, const char *state)
{
    jscctx_t *ctx = getctx (h);
    int64_t nstate = jsc_job_state2num (state);
    int64_t ostate = fetch_and_update_state (ctx->active_jobs, jobid, nstate);
    json_t *o;

    if (ostate < 0) {
        flux_log (h, LOG_INFO, "%"PRId64"'s old state unavailable", jobid);
        ostate = nstate;
    }
    if (!(o = json_pack ("{s:I s:{s:I s:I}}", JSC_JOBID, jobid,
                                              JSC_STATE_PAIR,
                                              JSC_STATE_PAIR_OSTATE, ostate,
                                              JSC_STATE_PAIR_NSTATE, nstate)))
        oom ();
    return o;
}


/******************************************************************************
 *                                                                            *
 *                 Internal Asynchronous Notification Mechanisms              *
 *                                                                            *
 ******************************************************************************/

static int invoke_cbs (flux_t *h, int64_t j, json_t *jcb, int errnum)
{
    int rc = 0;
    cb_pair_t *c = NULL;
    jscctx_t *ctx = getctx (h);
    for (c = zlist_first (ctx->callbacks); c; c = zlist_next (ctx->callbacks)) {
        char *jcb_str = json_dumps (jcb, 0);
        if (!jcb_str)
            oom ();
        if (c->cb (jcb_str, c->arg, errnum) < 0) {
            flux_log (h, LOG_DEBUG, "callback returns an error");
            rc = -1;
        }
        free (jcb_str);
    }
    return rc;
}

static json_t *get_reserve_jcb (flux_t *h, int64_t nj)
{
    json_t *ss = NULL;
    json_t *jcb = NULL;
    int64_t js = J_NULL;
    int64_t js2 = J_RESERVED;
    char *key = xasprintf ("%"PRId64, nj);
    jscctx_t *ctx = getctx (h);

    jcb = Jnew ();
    ss = Jnew ();
    Jadd_int64 (jcb, JSC_JOBID, nj);
    Jadd_int64 (ss, JSC_STATE_PAIR_OSTATE , (int64_t) js);
    Jadd_int64 (ss, JSC_STATE_PAIR_NSTATE, (int64_t) js2);
    if (json_object_set_new (jcb, JSC_STATE_PAIR, ss) < 0)
        oom ();
    if (zhash_insert (ctx->active_jobs, key, (void *)(intptr_t)js2) < 0) {
        flux_log (h, LOG_ERR, "%s: inserting a job to hash failed",
                  __FUNCTION__);
        goto done;
    }
    free (key);
    return jcb;

done:
    Jput (jcb);
    free (key);
    return NULL;
}

static json_t *get_submit_jcb (flux_t *h, const flux_msg_t *msg, int64_t jobid)
{
    jscctx_t *ctx = getctx (h);
    int64_t ntasks;
    int64_t nnodes;
    int64_t ncores;
    int64_t ngpus;
    int64_t walltime;
    int64_t ostate = J_NULL;
    int64_t nstate = J_SUBMITTED;
    json_t *jcb = NULL;
    char key[32];

    if (flux_event_unpack (msg, NULL, "{ s:I s:I s:I s:I s:I }",
                           "ntasks", &ntasks,
                           "nnodes", &nnodes,
                           "ncores", &ncores,
                           "ngpus", &ngpus,
                           "walltime", &walltime) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto error;
    }
    if (!(jcb = json_pack ("{s:I s:{s:I s:I} s:{s:I s:I s:I s:I s:I}}",
                           JSC_JOBID, jobid,
                           JSC_STATE_PAIR,
                           JSC_STATE_PAIR_OSTATE, ostate,
                           JSC_STATE_PAIR_NSTATE, nstate,
                           JSC_RDESC,
                           JSC_RDESC_NNODES, nnodes,
                           JSC_RDESC_NTASKS, ntasks,
                           JSC_RDESC_NCORES, ncores,
                           JSC_RDESC_NGPUS, ngpus,
                           JSC_RDESC_WALLTIME, walltime)))
        goto error;

    snprintf (key, sizeof (key), "%"PRId64, jobid);
    if (zhash_lookup (ctx->active_jobs, key)) {
        /* Note that we don't use the old state (reserved) in this case */
        zhash_update (ctx->active_jobs, key, (void *)(intptr_t)nstate);
    }
    else if (zhash_insert (ctx->active_jobs, key, (void *)(intptr_t)nstate) < 0) {
        flux_log (h, LOG_ERR, "%s: hash insertion failed", __FUNCTION__);
        goto error;
    }

    return jcb;
error:
    json_decref (jcb);
    return NULL;
}

static inline void delete_jobinfo (flux_t *h, int64_t jobid)
{
    jscctx_t *ctx = getctx (h);
    char *key = xasprintf ("%"PRId64, jobid);
    zhash_delete (ctx->active_jobs, key);
    free (key);
}

static bool job_is_finished (const char *state)
{
    if (strcmp (state, jsc_job_num2state (J_COMPLETE)) == 0
        || strcmp (state, jsc_job_num2state (J_FAILED)) == 0)
        return true;
    return false;
}

static void job_state_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    json_t *jcb = NULL;
    int64_t jobid = -1;
    const char *topic = NULL;
    const char *state = NULL;
    const char *kvs_path = NULL;
    int len = 12;

    if (flux_msg_get_topic (msg, &topic) < 0)
        goto done;

    if (flux_event_unpack (msg, NULL, "{ s:I }", "jobid", &jobid) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }

    if (!flux_event_unpack (msg, NULL, "{ s:s }", "kvs_path", &kvs_path)) {
        if (jscctx_add_jobid_path (getctx (h), jobid, kvs_path) < 0)
            flux_log_error (h, "jscctx_add_jobid_path");
    }

    if (strncmp (topic, "jsc", 3) == 0)
       len = 10;

    state = topic + len;
    if (strcmp (state, jsc_job_num2state (J_RESERVED)) == 0)
        jcb = get_reserve_jcb (h, jobid);
    else if (strcmp (state, jsc_job_num2state (J_SUBMITTED)) == 0)
        jcb = get_submit_jcb (h, msg, jobid);
    else
        jcb = get_update_jcb (h, jobid, state);

    if (invoke_cbs (h, jobid, jcb, (jcb)? 0 : EINVAL) < 0)
        flux_log (h, LOG_DEBUG, "%s: failed to invoke callbacks", __FUNCTION__);

    if (job_is_finished (state))
        delete_jobinfo (h, jobid);
done:
    if (jcb)
        Jput (jcb);
    return;
}

/******************************************************************************
 *                                                                            *
 *                    Public Job Status and Control API                       *
 *                                                                            *
 ******************************************************************************/
static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,     "wreck.state.*", job_state_cb, 0 },
    { FLUX_MSGTYPE_EVENT,     "jsc.state.*",   job_state_cb, 0 },
      FLUX_MSGHANDLER_TABLE_END
};

int jsc_notify_status (flux_t *h, jsc_handler_f func, void *d)
{
    int rc = -1;
    cb_pair_t *c = NULL;
    jscctx_t *ctx = NULL;
    flux_msg_handler_t **handlers;

    if (!func)
        goto done;
    if (flux_event_subscribe (h, "wreck.state.") < 0) {
        flux_log_error (h, "subscribing to job event");
        rc = -1;
        goto done;
    }
    if (flux_event_subscribe (h, "jsc.state.") < 0) {
        flux_log_error (h, "subscribing to job event");
        rc = -1;
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0) {
        flux_log_error (h, "registering resource event handler");
        rc = -1;
        goto done;
    }

    ctx = getctx (h);
    ctx->handlers = handlers;
    c = (cb_pair_t *) xzmalloc (sizeof(*c));
    c->cb = func;
    c->arg = d;
    if (zlist_append (ctx->callbacks, c) < 0)
        goto done;
    zlist_freefn (ctx->callbacks, c, free, true);
    rc = 0;

done:
    return rc;
}

int jsc_query_jcb (flux_t *h, int64_t jobid, const char *key, char **jcb_str)
{
    int rc = -1;
    json_t *jcb = NULL;

    if (!key) return -1;
    if (jobid_exist (h, jobid) != 0) return -1;

    if (!strcmp (key, JSC_JOBID)) {
        if ( (rc = query_jobid (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_jobid failed");
    } else if (!strcmp (key, JSC_STATE_PAIR)) {
        if ( (rc = query_state_pair (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_pdesc failed");
    } else if (!strcmp (key, JSC_RDESC)) {
        if ( (rc = query_rdesc (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_rdesc failed");
    } else if (!strcmp (key, JSC_RDL)) {
        if ( (rc = query_rdl (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_rdl failed");
    } else if (!strcmp (key, JSC_R_LITE)) {
        if ( (rc = query_r_lite (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_r_lite failed");
    } else if (!strcmp(key, JSC_PDESC)) {
        if ( (rc = query_pdesc (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_pdesc failed");
    } else
        flux_log (h, LOG_ERR, "key (%s) not understood", key);
    if (rc < 0)
        goto done;
    if (jcb) {
        char *s = json_dumps (jcb, 0);
        if (!s)
            oom ();
        *jcb_str = s;
    }
    else
        *jcb_str = NULL;
done:
    Jput (jcb);
    return rc;
}

int jsc_update_jcb (flux_t *h, int64_t jobid, const char *key,
        const char *jcb_str)
{
    int rc = -1;
    json_t *o = NULL;
    json_t *jcb = NULL;

    if (!jcb_str || !(jcb = Jfromstr (jcb_str))) {
        errno = EINVAL;
        return -1;
    }
    if (jobid_exist (h, jobid) != 0)
        goto done;

    if (!strcmp(key, JSC_JOBID)) {
        flux_log (h, LOG_ERR, "jobid attr cannot be updated");
    } else if (!strcmp (key, JSC_STATE_PAIR)) {
        if (Jget_obj (jcb, JSC_STATE_PAIR, &o))
            rc = update_state (h, jobid, o);
    } else if (!strcmp (key, JSC_RDESC)) {
        if (Jget_obj (jcb, JSC_RDESC, &o))
            rc = update_rdesc (h, jobid, o);
    } else if (!strcmp (key, JSC_RDL)) {
        if (Jget_obj (jcb, JSC_RDL, &o))
            rc = update_rdl (h, jobid, o);
    } else if (!strcmp (key, JSC_R_LITE)) {
        if (Jget_obj (jcb, JSC_R_LITE, &o))
            rc = update_r_lite (h, jobid, o);
    } else if (!strcmp (key, JSC_PDESC)) {
        if (Jget_obj (jcb, JSC_PDESC, &o))
            rc = update_pdesc (h, jobid, o);
    }
    else
        flux_log (h, LOG_ERR, "key (%s) not understood", key);
done:
    Jput (jcb);
    return rc;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
