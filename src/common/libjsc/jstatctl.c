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

#include "wreck.h"
#include "wreck_jobpath.h"
#include "jstatctl.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/shortjson.h"


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
    flux_msg_handler_t **handlers;
    zlist_t *callbacks;
    flux_t *h;
    struct wreck *wreck;
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
    { J_STOPPED,    "stopped" },
    { J_RUNNING,    "running" },
    { J_CANCELLED,  "cancelled" },
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
    zlist_destroy (&(ctx->callbacks));
    flux_msg_handler_delvec (ctx->handlers);
    wreck_destroy (ctx->wreck);
}

static jscctx_t *getctx (flux_t *h)
{
    jscctx_t *ctx = (jscctx_t *)flux_aux_get (h, "jstatctrl");
    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->callbacks = zlist_new ()))
            oom ();
        ctx->h = h;
        flux_aux_set (h, "jstatctrl", ctx, freectx);
    }
    return ctx;
}

json_object * kvspath_request_json (int64_t id)
{
    json_object *o = json_object_new_object ();
    json_object *ar = json_object_new_array ();
    json_object *v = json_object_new_int64 (id);

    if (!o || !ar || !v) {
        Jput (o);
        Jput (ar);
        Jput (v);
        return (NULL);
    }
    json_object_array_add (ar, v);
    json_object_object_add (o, "ids", ar);
    return (o);
}

const char * kvs_path_json_get (json_object *o)
{
    const char *p;
    json_object *ar;
    if (!Jget_obj (o, "paths", &ar) || !Jget_ar_str (ar, 0, &p))
        return (NULL);
    return (p);
}

/******************************************************************************
 *                                                                            *
 *                         Internal JCB Accessors                             *
 *                                                                            *
 ******************************************************************************/

static int jobid_exist (flux_t *h, int64_t j)
{
    flux_future_t *f = NULL;
    char buf[WRECK_MAX_JOB_PATH];
    const char *path = wreck_id_to_path (h, buf, sizeof (buf), j);
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

static bool fetch_rank_pdesc (json_object *src, int64_t *p, int64_t *n,
			      const char **c)
{
    if (!src) return false;
    if (!Jget_str (src, "command", c)) return false;
    if (!Jget_int64 (src, "pid", p)) return false;
    if (!Jget_int64 (src, "nodeid", n)) return false;
    return true;
}

static int build_name_array (zhash_t *ha, const char *k, json_object *ns)
{
    int i = (intptr_t) zhash_lookup (ha, k);
    if ((void *)((intptr_t)i) == NULL) {
        char *t = xstrdup (k);
        i = json_object_array_length (ns);
        Jadd_ar_str (ns, t);
        zhash_insert (ha, k, (void *)(intptr_t)i+1);
        free (t);
    } else
        i--;
    return i;
}

static char * lwj_key (flux_t *h, char *buf, int bufsz,
                       int64_t id, const char *fmt, ...)
{
    va_list ap;
    int rc;
    int len;

    if (!wreck_id_to_path (h, buf, bufsz, id))
        return (NULL);

    if (fmt != NULL) {
        len = strlen (buf);
        va_start (ap, fmt);
        rc = vsnprintf (buf + len, bufsz - len, fmt, ap);
        va_end (ap);
        if (rc >= bufsz - len)
            return (NULL);
    }
    return (buf);
}

static int extract_raw_nnodes (flux_t *h, int64_t j, int64_t *nnodes)
{
    int rc = 0;
    char buf[WRECK_MAX_JOB_PATH];
    char *key = lwj_key (h, buf, sizeof (buf), j, ".nnodes");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "I", nnodes) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *nnodes);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_ntasks (flux_t *h, int64_t j, int64_t *ntasks)
{
    int rc = 0;
    char buf[WRECK_MAX_JOB_PATH];
    char *key = lwj_key (h, buf, sizeof (buf), j, ".ntasks");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "I", ntasks) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *ntasks);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_walltime (flux_t *h, int64_t j, int64_t *walltime)
{
    int rc = 0;
    char buf[WRECK_MAX_JOB_PATH];
    char *key = lwj_key (h, buf, sizeof (buf), j, ".walltime");
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "I", walltime) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *walltime);
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_rdl (flux_t *h, int64_t j, char **rdlstr)
{
    int rc = 0;
    char buf[WRECK_MAX_JOB_PATH];
    char *key = lwj_key (h, buf, sizeof (buf), j, ".rdl");
    const char *s;
    flux_future_t *f = NULL;

    if (!key || !(f = flux_kvs_lookup (h, 0, key))
             || flux_kvs_lookup_get_unpack (f, "s", &s) < 0) {
        flux_log_error (h, "extract %s", key);
        rc = -1;
    }
    else {
        *rdlstr = xstrdup (s);
        flux_log (h, LOG_DEBUG, "rdl under %s extracted", key);
    }
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_state (flux_t *h, int64_t j, int64_t *s)
{
    int rc = 0;
    char buf[WRECK_MAX_JOB_PATH];
    char *key = lwj_key (h, buf, sizeof (buf), j, ".state");
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
    flux_future_destroy (f);
    return rc;
}

static int extract_raw_pdesc (flux_t *h, int64_t j, int64_t i, json_object **o)
{
    char buf[WRECK_MAX_JOB_PATH];
    flux_future_t *f = NULL;
    const char *json_str;
    char *key = lwj_key (h, buf, sizeof (buf), j, ".%ju.procdesc", i);
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
    return rc;
}

static json_object *build_parray_elem (int64_t pid, int64_t eix, int64_t hix)
{
    json_object *po = Jnew ();
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_PID, pid);
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_EINDX, eix);
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_HINDX, hix);
    return po;
}

static void add_pdescs_to_jcb (json_object **hns, json_object **ens,
			       json_object **pa, json_object *jcb)
{
    json_object_object_add (jcb, JSC_PDESC_HOSTNAMES, *hns);
    json_object_object_add (jcb, JSC_PDESC_EXECS, *ens);
    json_object_object_add (jcb, JSC_PDESC_PDARRAY, *pa);
    /* Because the above transfer ownership, assign NULL should be ok */
    *hns = NULL;
    *ens = NULL;
    *pa = NULL;
}

static int extract_raw_pdescs (flux_t *h, int64_t j, int64_t n, json_object *jcb)
{
    int rc = -1;
    int64_t i = 0;
    char *hnm;
    const char *cmd = NULL;
    zhash_t *eh = NULL; /* hash holding a set of unique exec_names */
    zhash_t *hh = NULL; /* hash holding a set of unique host_names */
    json_object *o = NULL;
    json_object *po = NULL;
    json_object *pa = Jnew_ar ();
    json_object *hns = Jnew_ar ();
    json_object *ens = Jnew_ar ();

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
        json_object_array_add (pa, po);
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

static int extract_raw_rdl_alloc (flux_t *h, int64_t j, json_object *jcb)
{
    char buf[WRECK_MAX_JOB_PATH];
    int i;
    json_object *ra = Jnew_ar ();
    bool processing = true;

    for (i=0; processing; ++i) {
        char *key = lwj_key (h, buf, sizeof (buf), j, ".rank.%d.cores", i);
        flux_future_t *f = NULL;
        int64_t cores = 0;
        if (!key || !(f = flux_kvs_lookup (h, 0, key))
                 || flux_kvs_lookup_get_unpack (f, "I", &cores) < 0) {
            if (errno != EINVAL)
                flux_log_error (h, "extract %s", key);
            processing = false;
        } else {
            json_object *elem = Jnew ();
            json_object *o = Jnew ();
            Jadd_int64 (o, JSC_RDL_ALLOC_CONTAINED_NCORES, cores);
            json_object_object_add (elem, JSC_RDL_ALLOC_CONTAINED, o);
            json_object_array_add (ra, elem);
        }
        flux_future_destroy (f);
    }
    json_object_object_add (jcb, JSC_RDL_ALLOC, ra);
    return 0;
}

static int query_jobid (flux_t *h, int64_t j, json_object **jcb)
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

static int query_state_pair (flux_t *h, int64_t j, json_object **jcb)
{
    json_object *o = NULL;
    int64_t st = (int64_t)J_FOR_RENT;;

    if (extract_raw_state (h, j, &st) < 0) return -1;

    *jcb = Jnew ();
    o = Jnew ();
    /* Old state is unavailable through the query.
     * One should use notification service instead.
     */
    Jadd_int64 (o, JSC_STATE_PAIR_OSTATE, st);
    Jadd_int64 (o, JSC_STATE_PAIR_NSTATE, st);
    json_object_object_add (*jcb, JSC_STATE_PAIR, o);
    return 0;
}

static int query_rdesc (flux_t *h, int64_t j, json_object **jcb)
{
    json_object *o = NULL;
    int64_t nnodes = -1;
    int64_t ntasks = -1;
    int64_t walltime = -1;

    if (extract_raw_nnodes (h, j, &nnodes) < 0) return -1;
    if (extract_raw_ntasks (h, j, &ntasks) < 0) return -1;
    if (extract_raw_walltime (h, j, &walltime) < 0) return -1;

    *jcb = Jnew ();
    o = Jnew ();
    Jadd_int64 (o, JSC_RDESC_NNODES, nnodes);
    Jadd_int64 (o, JSC_RDESC_NTASKS, ntasks);
    Jadd_int64 (o, JSC_RDESC_WALLTIME, walltime);
    json_object_object_add (*jcb, JSC_RDESC, o);
    return 0;
}

static int query_rdl (flux_t *h, int64_t j, json_object **jcb)
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

static int query_rdl_alloc (flux_t *h, int64_t j, json_object **jcb)
{
    *jcb = Jnew ();
    return extract_raw_rdl_alloc (h, j, *jcb);
}

static int query_pdesc (flux_t *h, int64_t j, json_object **jcb)
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
    if ((msg = flux_event_pack (topic, "{ s:I }", "lwj", j)) == NULL) {
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

static int update_state (flux_t *h, int64_t j, json_object *o)
{
    int rc = -1;
    char buf[WRECK_MAX_JOB_PATH];
    int64_t st = 0;
    char *key = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!Jget_int64 (o, JSC_STATE_PAIR_NSTATE, &st))
        goto done;
    if ((st >= J_FOR_RENT) || (st < J_NULL))
        goto done;
    if (!(key = lwj_key (h, buf, sizeof (buf), j, ".state")))
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
    return rc;
}

static int update_rdesc (flux_t *h, int64_t j, json_object *o)
{
    int rc = -1;
    char buf[WRECK_MAX_JOB_PATH];
    int64_t nnodes = 0;
    int64_t ntasks = 0;
    int64_t walltime = 0;
    char *key;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!Jget_int64 (o, JSC_RDESC_NNODES, &nnodes))
        goto done;
    if (!Jget_int64 (o, JSC_RDESC_NTASKS, &ntasks))
        goto done;
    if (!Jget_int64 (o, JSC_RDESC_WALLTIME, &walltime))
        goto done;
    if ((nnodes < 0) || (ntasks < 0) || (walltime < 0))
        goto done;
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    if (!(key = lwj_key (h, buf, sizeof (buf), j, ".nnodes")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "I", nnodes) < 0) {
        flux_log_error (h, "update %s", key);
        goto done;
    }
    if (!(key = lwj_key (h, buf, sizeof (buf), j, ".ntasks")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "I", ntasks) < 0) {
        flux_log_error (h, "update %s", key);
        goto done;
    }
    if (!(key = lwj_key (h, buf, sizeof (buf), j, ".walltime")))
        goto done;
    if (flux_kvs_txn_pack (txn, 0, key, "I", walltime) < 0) {
        flux_log_error (h, "update %s", key);
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

    return rc;
}

static int update_rdl (flux_t *h, int64_t j, const char *rs)
{
    int rc = -1;
    char buf[WRECK_MAX_JOB_PATH];
    char *key = lwj_key (h, buf, sizeof (buf), j, ".rdl");
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!key)
        goto done;
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    if (flux_kvs_txn_pack (txn, 0, key, "s", rs) < 0) {
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

    return rc;
}

static int update_hash_1ra (flux_t *h, int64_t j, json_object *o, zhash_t *rtab)
{
    char buf[WRECK_MAX_JOB_PATH];
    int64_t ncores = 0;
    int64_t rank = 0;
    int64_t *curcnt;
    char *key;
    json_object *c = NULL;

    if (!Jget_obj (o, JSC_RDL_ALLOC_CONTAINED, &c)) return -1;
    if (!Jget_int64 (c, JSC_RDL_ALLOC_CONTAINING_RANK, &rank)) return -1;
    if (!Jget_int64 (c, JSC_RDL_ALLOC_CONTAINED_NCORES, &ncores)) return -1;

    if (!(key = lwj_key (h, buf, sizeof (buf), j, ".rank.%ju.cores", rank)))
        return -1;
    if (!(curcnt = zhash_lookup (rtab, key))) {
        curcnt = xzmalloc (sizeof (*curcnt));
        *curcnt = ncores;
        zhash_insert (rtab, key, curcnt);
        zhash_freefn (rtab, key, free);
    } else
        *curcnt = *curcnt + ncores;
    return 0;
}

static int update_rdl_alloc (flux_t *h, int64_t j, json_object *o)
{
    int i = 0;
    int rc = -1;
    int size = 0;
    json_object *ra_e = NULL;
    const char *key = NULL;
    zhash_t *rtab = NULL;
    int64_t *ncores = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!(rtab = zhash_new ()))
        oom ();
    if (!Jget_ar_len (o, &size))
        goto done;

    for (i=0; i < (int) size; ++i) {
        if (!Jget_ar_obj (o, i, &ra_e))
            goto done;
        /* 'o' represents an array of per-node core count to use.
         * However, becasue the same rank can appear multiple times
         * in this array in emulation mode, update_hash_1ra is
         * used to determine the total core count per rank.
         */
        if ( (rc = update_hash_1ra (h, j, ra_e, rtab)) < 0)
            goto done;
    }

    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "txn_create");
        goto done;
    }
    FOREACH_ZHASH (rtab, key, ncores) {
        if ( (rc = flux_kvs_txn_pack (txn, 0, key, "I", *ncores)) < 0) {
            flux_log_error (h, "put %s", key);
            goto done;
        }
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log (h, LOG_ERR, "update_pdesc commit failed");
        goto done;
    }
    rc = 0;

done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    zhash_destroy (&rtab);
    return rc;
}

static int update_1pdesc (flux_t *h, flux_kvs_txn_t *txn,
              int r, int64_t j, json_object *o,
			  json_object *ha, json_object *ea)
{
    char buf[WRECK_MAX_JOB_PATH];
    flux_future_t *f = NULL;
    int rc = -1;
    json_object *d = NULL;
    char *key;
    const char *json_str;
    const char *hn = NULL, *en = NULL;
    int64_t pid = 0, hindx = 0, eindx = 0, hrank = 0;

    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_PID, &pid)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_HINDX, &hindx)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_EINDX, &eindx)) return -1;
    if (!Jget_ar_str (ha, (int)hindx, &hn)) return -1;
    if (!Jget_ar_str (ea, (int)eindx, &en)) return -1;

    key = lwj_key (h, buf, sizeof (buf), j, ".%d.procdesc", r);

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
    if (flux_kvs_txn_put (txn, 0, key, Jtostr (d)) < 0) {
        flux_log_error (h, "put %s", key);
        goto done;
    }
    rc = 0;

done:
    flux_future_destroy (f);
    if (d)
        Jput (d);
    return rc;
}

static int update_pdesc (flux_t *h, int64_t j, json_object *o)
{
    int i = 0;
    int rc = -1;
    int64_t size = 0;
    json_object *h_arr = NULL;
    json_object *e_arr = NULL;
    json_object *pd_arr = NULL;
    json_object *pde = NULL;
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


/******************************************************************************
 *                                                                            *
 *                 Internal Asynchronous Notification Mechanisms              *
 *                                                                            *
 ******************************************************************************/

static json_object *get_update_jcb (int64_t id, int64_t ostate, int64_t nstate)
{
    json_object *o = Jnew ();
    json_object *ss = Jnew ();

    Jadd_int64 (o, JSC_JOBID, id);
    Jadd_int64 (ss, JSC_STATE_PAIR_OSTATE , ostate);
    Jadd_int64 (ss, JSC_STATE_PAIR_NSTATE,  nstate);
    json_object_object_add (o, JSC_STATE_PAIR, ss);

    return o;
}

static int invoke_cbs (flux_t *h, int64_t j, json_object *jcb, int errnum)
{
    int rc = 0;
    cb_pair_t *c = NULL;
    jscctx_t *ctx = getctx (h);
    for (c = zlist_first (ctx->callbacks); c; c = zlist_next (ctx->callbacks)) {
        if (c->cb (Jtostr (jcb), c->arg, errnum) < 0) {
            flux_log (h, LOG_ERR, "callback returns an error");
            rc = -1;
        }
    }
    return rc;
}

/* Handle the jsc state changes.
 */
static void job_state_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    jscctx_t *ctx = arg;
    json_object *jcb = NULL;
    int64_t jobid;
    const char *topic;
    const char *state;
    struct wreck_job *job;
    int prev_state;

    if (flux_event_unpack (msg, &topic, "{ s:I }", "lwj", &jobid) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        return;
    }
    state = topic + 10; // jsc.state.<state>  (10 chars)
    if (!(job = wreck_job_lookup (ctx->wreck, jobid))) {
        flux_log (h, LOG_ERR, "%s: id=%lld state=%s not an active job",
                  __FUNCTION__, (long long)jobid, state);
        return;
    }
    prev_state = job->jsc_state;
    job->jsc_state = jsc_job_state2num (state);

    jcb = get_update_jcb (jobid, prev_state, job->jsc_state);
    if (invoke_cbs (h, jobid, jcb, 0) < 0)
        flux_log (h, LOG_ERR, "job_state_cb: failed to invoke callbacks");
    Jput (jcb);
    wreck_job_decref (job);
}

/* Handle the wreck job state changes.
 * N.B. job->state numbering does not match J_<state> values.
 */
static void job_state_wreck_cb (struct wreck_job *job, void *arg)
{
    jscctx_t *ctx = arg;
    const char *state = wreck_state2str (job->state);
    json_object *jcb;
    int prev_state;

    if (job->state == WRECK_STATE_RESERVED
                            || job->state == WRECK_STATE_SUBMITTED)
        prev_state = J_NULL; // new job, job->jsc_state not initialized
    else
        prev_state = job->jsc_state;
    job->jsc_state = jsc_job_state2num (state);

    jcb = get_update_jcb (job->id, prev_state, job->jsc_state);
    if (invoke_cbs (ctx->h, job->id, jcb, 0) < 0)
        flux_log (ctx->h, LOG_ERR, "job_state_cb: failed to invoke callbacks");
    Jput (jcb);
}

/******************************************************************************
 *                                                                            *
 *                    Public Job Status and Control API                       *
 *                                                                            *
 ******************************************************************************/
static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,     "jsc.state.*",   job_state_cb, 0 },
      FLUX_MSGHANDLER_TABLE_END
};

int jsc_notify_status (flux_t *h, jsc_handler_f func, void *d)
{
    int rc = -1;
    cb_pair_t *c = NULL;
    jscctx_t *ctx = NULL;

    if (!func) {
        errno = EINVAL;
        goto done;
    }
    ctx = getctx (h);
    if (!ctx->handlers) {
        if (flux_event_subscribe (h, "jsc.state.") < 0) {
            flux_log_error (h, "subscribing to job event");
            goto done;
        }
        if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0) {
            flux_log_error (h, "registering resource event handler");
            goto done;
        }
        if (!(ctx->wreck = wreck_create (h))) {
            flux_log_error (h, "wreck_create failed");
            goto done;
        }
        if (wreck_set_notify (ctx->wreck, WRECK_STATE_ALL,
                              job_state_wreck_cb, ctx) < 0) {
            flux_log_error (h, "wreck_set_notify failed");
            goto done;
        }
    }

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
    json_object *jcb = NULL;

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
    } else if (!strcmp (key, JSC_RDL_ALLOC)) {
        if ( (rc = query_rdl_alloc (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_rdl_alloc failed");
    } else if (!strcmp(key, JSC_PDESC)) {
        if ( (rc = query_pdesc (h, jobid, &jcb)) < 0)
            flux_log (h, LOG_ERR, "query_pdesc failed");
    } else
        flux_log (h, LOG_ERR, "key (%s) not understood", key);
    if (rc < 0)
        goto done;
    *jcb_str = jcb ? xstrdup (Jtostr (jcb)) : NULL;
done:
    Jput (jcb);
    return rc;
}

int jsc_update_jcb (flux_t *h, int64_t jobid, const char *key,
        const char *jcb_str)
{
    int rc = -1;
    json_object *o = NULL;
    json_object *jcb = NULL;

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
        const char *s = NULL;
        if (Jget_str (jcb, JSC_RDL, &s))
            rc = update_rdl (h, jobid, s);
    } else if (!strcmp (key, JSC_RDL_ALLOC)) {
        if (Jget_obj (jcb, JSC_RDL_ALLOC, &o))
            rc = update_rdl_alloc (h, jobid, o);
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
