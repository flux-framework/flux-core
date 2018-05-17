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
#include <jansson.h>

#include "jstatctl.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/lru_cache.h"


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

static int *active_insert (zhash_t *hash, int64_t jobid)
{
    char key[32];
    int *state;
    snprintf (key, sizeof (key), "%"PRId64, jobid);
    if (!(state = calloc (1, sizeof (int))))
        return NULL;
    if (zhash_insert (hash, key, state) < 0) {
        free (state);
        return NULL;
    }
    zhash_freefn (hash, key, (zhash_free_fn *)free);
    return state;
}

static int *active_lookup (zhash_t *hash, int64_t jobid)
{
    char key[32];
    snprintf (key, sizeof (key), "%"PRId64, jobid);
    return zhash_lookup (hash, key);
}

static void active_delete (zhash_t *hash, int64_t jobid)
{
    char key[32];
    snprintf (key, sizeof (key), "%"PRId64, jobid);
    return zhash_delete (hash, key);
}

/******************************************************************************
 *                                                                            *
 *                         Internal JCB Accessors                             *
 *                                                                            *
 ******************************************************************************/

static int jobid_exist (flux_t *h, int64_t jobid)
{
    jscctx_t *ctx = getctx (h);
    flux_future_t *f = NULL;
    const char *path;
    int rc = -1;

    if (!active_lookup (ctx->active_jobs, jobid)) {
        if (!(path = jscctx_jobid_path (ctx, jobid)))
            goto done;
        if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, path))
                                    || flux_future_get (f, NULL) < 0)
            goto done;
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static char *lwj_vkey (flux_t *h, int64_t jobid, const char *fmt, va_list ap)
{
    jscctx_t *ctx = getctx (h);
    const char *base;
    char *name;
    char *key;

    if (!(base = jscctx_jobid_path (ctx, jobid)))
        return (NULL);
    if (vasprintf (&name, fmt, ap) < 0)
        return (NULL);
    if (asprintf (&key, "%s%s", base, name) < 0) {
        free (name);
        return (NULL);
    }
    free (name);
    return (key);
}

static char * __attribute__ ((format (printf, 3, 4)))
lwj_key (flux_t *h, int64_t jobid, const char *fmt, ...)
{
    va_list ap;
    char *key;

    va_start (ap, fmt);
    key = lwj_vkey (h, jobid, fmt, ap);
    va_end (ap);
    return key;
}

static flux_future_t * __attribute__ ((format (printf, 3, 4)))
lookup_job_attribute (flux_t *h, int64_t jobid, const char *fmt, ...)
{
    va_list ap;
    flux_future_t *f;
    char *key;

    va_start (ap, fmt);
    key = lwj_vkey (h, jobid, fmt, ap);
    va_end (ap);
    if (!key)
        return NULL;
    f = flux_kvs_lookup (h, 0, key);
    free (key);
    return f;
}

/* Old state is unavailable through the query.
 * One should use notification service instead.
 */
static json_t *query_state_pair (flux_t *h, int64_t jobid)
{
    json_t *jcb = NULL;
    char *key;
    int64_t state;
    const char *state_str;
    flux_future_t *f = NULL;

    if (!(key = lwj_key (h, jobid, ".state")))
        return NULL;

    if (!(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "s", &state_str) < 0)
        goto done;
    state = jsc_job_state2num (state_str);
    if (!(jcb = json_pack ("{s:{s:I s:I}}",
                           JSC_STATE_PAIR,
                           JSC_STATE_PAIR_OSTATE, state,
                           JSC_STATE_PAIR_NSTATE, state)))
        goto done;
done:
    flux_future_destroy (f);
    free (key);
    return jcb;
}

static json_t *query_rdesc (flux_t *h, int64_t jobid)
{
    json_t *jcb = NULL;
    int64_t nnodes;
    int64_t ntasks;
    int64_t ncores;
    int64_t ngpus;
    int64_t walltime;

    if (jsc_query_rdesc_efficiently (h, jobid, &nnodes, &ntasks, &ncores,
                                     &walltime, &ngpus) < 0)
        return NULL;

    if (!(jcb = json_pack ("{s:{s:I s:I s:I s:I s:I}}",
                           JSC_RDESC,
                           JSC_RDESC_NNODES, nnodes,
                           JSC_RDESC_NTASKS, ntasks,
                           JSC_RDESC_NCORES, ncores,
                           JSC_RDESC_WALLTIME, walltime,
                           JSC_RDESC_NGPUS, ngpus)))
        return NULL;
    return jcb;
}

int jsc_query_rdesc_efficiently (flux_t *h, int64_t jobid,
                                 int64_t *nnodes, int64_t *ntasks,
                                 int64_t *ncores, int64_t *walltime,
                                 int64_t *ngpus)
{
    flux_future_t *f_nnodes = NULL;
    flux_future_t *f_ntasks = NULL;
    flux_future_t *f_ncores = NULL;
    flux_future_t *f_wall = NULL;
    flux_future_t *f_ngpus = NULL;
    int rc = -1;

    /* Send requests (in parallel)
     */
    if (nnodes && !(f_nnodes = lookup_job_attribute (h, jobid, ".nnodes")))
        goto error;
    if (ntasks && !(f_ntasks = lookup_job_attribute (h, jobid, ".ntasks")))
        goto error;
    if (ncores && !(f_ncores = lookup_job_attribute (h, jobid, ".ncores")))
        goto error;
    if (walltime && !(f_wall = lookup_job_attribute (h, jobid, ".walltime")))
        goto error;
    if (ngpus && !(f_ngpus = lookup_job_attribute (h, jobid, ".ngpus")))
        goto error;

    /* Handle responses
     */
    if (f_nnodes && flux_kvs_lookup_get_unpack (f_nnodes, "I", nnodes) < 0)
        goto error;
    if (f_ntasks && flux_kvs_lookup_get_unpack (f_ntasks, "I", ntasks) < 0)
        goto error;
    if (f_ncores && flux_kvs_lookup_get_unpack (f_ncores, "I", ncores) < 0)
        goto error;
    if (f_wall && flux_kvs_lookup_get_unpack (f_wall, "I", walltime) < 0)
        goto error;
    if (f_ngpus && flux_kvs_lookup_get_unpack (f_ngpus, "I", ngpus) < 0)
        goto error;

    rc = 0;
error:
    flux_future_destroy (f_nnodes);
    flux_future_destroy (f_ntasks);
    flux_future_destroy (f_ncores);
    flux_future_destroy (f_wall);
    flux_future_destroy (f_ngpus);
    return rc;
}

static json_t *query_rdl (flux_t *h, int64_t jobid)
{
    flux_future_t *f;
    const char *json_str;
    json_t *rdl;
    json_t *jcb = NULL;

    if (!(f = lookup_job_attribute (h, jobid, ".rdl")))
        return NULL;
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        goto error;
    if (!(rdl = json_loads (json_str, 0, NULL)))
        goto error;
    if (!(jcb = json_pack ("{s:o}",
                           JSC_RDL, rdl))) {
        json_decref (rdl);
        goto error;
    }
error:
    flux_future_destroy (f);
    return jcb;
}

static json_t *query_r_lite (flux_t *h, int64_t jobid)
{
    flux_future_t *f;
    const char *json_str;
    json_t *r_lite;
    json_t *jcb = NULL;

    if (!(f = lookup_job_attribute (h, jobid, ".R_lite")))
        return NULL;
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        goto error;
    if (!(r_lite = json_loads (json_str, 0, NULL)))
        goto error;
    if (!(jcb = json_pack ("{s:o}",
                           JSC_R_LITE, r_lite))) {
        json_decref (r_lite);
        goto error;
    }
error:
    flux_future_destroy (f);
    return jcb;
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

static int update_state (flux_t *h, int64_t jobid, json_t *jcb)
{
    int rc = -1;
    int state;
    char *key = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (json_unpack (jcb, "{s:{s:i}}",
                          JSC_STATE_PAIR,
                          JSC_STATE_PAIR_NSTATE, &state) < 0)
        goto done;
    if ((state >= J_FOR_RENT) || (state < J_NULL))
        goto done;
    if (!(key = lwj_key (h, jobid, ".state")))
        goto done;
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key, "s", jsc_job_num2state (state)) < 0) {
        flux_log_error (h, "update %s", key);
        goto done;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit %s", key);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new state: %s", jobid,
          jsc_job_num2state (state));
    rc = 0;

    if (send_state_event (h, state, jobid) < 0)
        flux_log_error (h, "send state event");

done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (key);
    return rc;
}

static int update_rdesc (flux_t *h, int64_t jobid, json_t *jcb)
{
    int rc = -1;
    int64_t nnodes;
    int64_t ntasks;
    int64_t ncores;
    int64_t walltime;
    char *key = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (json_unpack (jcb, "{s:{s:I s:I s:I s:I}}",
                          JSC_RDESC,
                          JSC_RDESC_NNODES, &nnodes,
                          JSC_RDESC_NTASKS, &ntasks,
                          JSC_RDESC_NCORES, &ncores,
                          JSC_RDESC_WALLTIME, &walltime) < 0)
        goto done;
    if ((nnodes < 0) || (ntasks < 0) || (ncores < 0) || (walltime < 0))
        goto done;
    if (!(txn = flux_kvs_txn_create ()))
        goto done;
    if (!(key = lwj_key (h, jobid, ".nnodes")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "I", nnodes) < 0)
        goto done;
    free (key);
    if (!(key = lwj_key (h, jobid, ".ntasks")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "I", ntasks) < 0)
        goto done;
    free (key);
    if (!(key = lwj_key (h, jobid, ".walltime")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "I", walltime) < 0)
        goto done;
    free (key);
    if (!(key = lwj_key (h, jobid, ".ncores")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "I", ncores) < 0)
        goto done;
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    free (key);
    flux_kvs_txn_destroy (txn);
    return rc;
}

static int update_rdl (flux_t *h, int64_t jobid, json_t *jcb)
{
    int rc = -1;
    char *key = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    json_t *rdl;

    if (json_unpack (jcb, "{s:o}", JSC_RDL, &rdl) < 0)
        goto done;
    if (!(key = lwj_key (h, jobid, ".rdl")))
        goto done;
    if (!(txn = flux_kvs_txn_create ()))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "O", rdl) < 0)
        goto done;
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    free (key);
    return rc;
}

static int update_r_lite (flux_t *h, int64_t jobid, json_t *jcb)
{
    int rc = -1;
    char *key = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    json_t *r_lite;

    if (json_unpack (jcb, "{s:o}", JSC_R_LITE, &r_lite) < 0)
        goto done;
    if (!(txn = flux_kvs_txn_create ()))
        goto done;
    if (!(key = lwj_key (h, jobid, ".R_lite")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "O", r_lite) < 0)
        goto done;
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    free (key);
    return rc;
}

static json_t *get_update_jcb (flux_t *h, int64_t jobid, const char *state_str)
{
    jscctx_t *ctx = getctx (h);
    int *state;
    int nstate = jsc_job_state2num (state_str);
    int ostate = J_FOR_RENT;
    json_t *o;

    if ((state = active_lookup (ctx->active_jobs, jobid))) {
        ostate = *state;
        if (nstate == J_COMPLETE || nstate == J_FAILED)
            active_delete (ctx->active_jobs, jobid);
        else
            *state = nstate;
    }
    if (!(o = json_pack ("{s:I s:{s:i s:i}}", JSC_JOBID, jobid,
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

static json_t *get_reserve_jcb (flux_t *h, int64_t jobid)
{
    jscctx_t *ctx = getctx (h);
    json_t *jcb;
    int ostate = J_NULL;
    int nstate = J_RESERVED;
    int *state;

    if (!(jcb = json_pack ("{s:I s{s:i s:i}}",
                           JSC_JOBID, jobid,
                           JSC_STATE_PAIR,
                           JSC_STATE_PAIR_OSTATE, ostate,
                           JSC_STATE_PAIR_NSTATE, nstate)))
        goto error;

    if (!(state = active_insert (ctx->active_jobs, jobid)))
        goto error;
    *state = nstate;
    return jcb;

error:
    json_decref (jcb);
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
    int ostate = J_NULL;
    int nstate = J_SUBMITTED;
    int *state;
    json_t *jcb = NULL;

    if (flux_event_unpack (msg, NULL, "{ s:I s:I s:I s:I s:I }",
                           "ntasks", &ntasks,
                           "nnodes", &nnodes,
                           "ncores", &ncores,
                           "ngpus", &ngpus,
                           "walltime", &walltime) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto error;
    }
    if (!(jcb = json_pack ("{s:I s:{s:i s:i} s:{s:I s:I s:I s:I s:I}}",
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

    if (!(state = active_lookup (ctx->active_jobs, jobid))) {
        if (!(state = active_insert (ctx->active_jobs, jobid)))
            goto error;
    }
    *state = nstate;

    return jcb;
error:
    json_decref (jcb);
    return NULL;
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
    jscctx_t *ctx = getctx (h);
    json_t *jcb = NULL;
    int64_t jobid;
    const char *topic = NULL;
    const char *state = NULL;
    const char *kvs_path = NULL;
    int len = 12;

    if (flux_event_unpack (msg, &topic, "{s:I s?:s}",
                                        "jobid", &jobid,
                                        "kvs_path", &kvs_path) < 0)
        goto done;
    if (kvs_path && jscctx_add_jobid_path (ctx, jobid, kvs_path) < 0)
        goto done;

    if (strncmp (topic, "jsc", 3) == 0)
       len = 10;

    state = topic + len;
    if (strcmp (state, jsc_job_num2state (J_RESERVED)) == 0)
        jcb = get_reserve_jcb (h, jobid);
    else if (strcmp (state, jsc_job_num2state (J_SUBMITTED)) == 0)
        jcb = get_submit_jcb (h, msg, jobid);
    else
        jcb = get_update_jcb (h, jobid, state);

    (void)invoke_cbs (h, jobid, jcb, (jcb)? 0 : EINVAL);

    if (job_is_finished (state))
        active_delete (ctx->active_jobs, jobid);
done:
    json_decref (jcb);
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
    json_t *jcb = NULL;

    if (!key)
        return -1;
    if (jobid_exist (h, jobid) < 0)
        return -1;

    if (!strcmp (key, JSC_JOBID))
        jcb = json_pack ("{s:I}", JSC_JOBID, jobid);
    else if (!strcmp (key, JSC_STATE_PAIR))
        jcb = query_state_pair (h, jobid);
    else if (!strcmp (key, JSC_RDESC))
        jcb = query_rdesc (h, jobid);
    else if (!strcmp (key, JSC_RDL))
        jcb = query_rdl (h, jobid);
    else if (!strcmp (key, JSC_R_LITE))
        jcb = query_r_lite (h, jobid);

    if (!jcb)
        return -1;
    char *s = json_dumps (jcb, 0);
    if (!s) {
        json_decref (jcb);
        return -1;
    }
    *jcb_str = s;
    return 0;
}

int jsc_update_jcb (flux_t *h, int64_t jobid, const char *key,
        const char *jcb_str)
{
    int rc = -1;
    json_t *jcb;

    if (!jcb_str || !(jcb = json_loads (jcb_str, 0, NULL)))
        return -1;
    if (jobid_exist (h, jobid) != 0)
        goto done;
    if (!strcmp (key, JSC_STATE_PAIR))
        rc = update_state (h, jobid, jcb);
    else if (!strcmp (key, JSC_RDESC))
        rc = update_rdesc (h, jobid, jcb);
    else if (!strcmp (key, JSC_RDL))
        rc = update_rdl (h, jobid, jcb);
    else if (!strcmp (key, JSC_R_LITE))
        rc = update_r_lite (h, jobid, jcb);
done:
    json_decref (jcb);
    return rc;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
