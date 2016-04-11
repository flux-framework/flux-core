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
#include "jstatctl_deprecated.h"
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
   jsc_handler_obj_f cb;
   void *arg;
} cb_pair_t;

typedef struct {
    zhash_t *active_jobs;
    zlist_t *callbacks;
    int first_time;
    flux_t h;
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

static int jsc_job_state2num (const char *s)
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
    zlist_destroy (&(ctx->callbacks));
}

static jscctx_t *getctx (flux_t h)
{
    jscctx_t *ctx = (jscctx_t *)flux_aux_get (h, "jstatctrl");
    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->active_jobs = zhash_new ()))
            oom ();
        if (!(ctx->callbacks = zlist_new ()))
            oom ();
        ctx->first_time = 1;
        ctx->h = h;
        flux_aux_set (h, "jstatctrl", ctx, freectx);
    }
    return ctx;
}

static inline bool is_jobid (const char *k)
{
    return (!strncmp (JSC_JOBID, k, JSC_MAX_ATTR_LEN))? true : false;
}

static inline bool is_state_pair (const char *k)
{
    return (!strncmp (JSC_STATE_PAIR, k, JSC_MAX_ATTR_LEN))? true : false;
}

static inline bool is_rdesc (const char *k)
{
    return (!strncmp (JSC_RDESC, k, JSC_MAX_ATTR_LEN))? true : false;
}

static inline bool is_rdl (const char *k)
{
    return (!strncmp (JSC_RDL, k, JSC_MAX_ATTR_LEN))? true : false;
}

static inline bool is_rdl_alloc (const char *k)
{
    return (!strncmp (JSC_RDL_ALLOC, k, JSC_MAX_ATTR_LEN))? true : false;
}

static inline bool is_pdesc (const char *k)
{
    return (!strncmp (JSC_PDESC, k, JSC_MAX_ATTR_LEN))? true : false;
}

static int fetch_and_update_state (zhash_t *aj , int64_t j, int64_t ns)
{
    int *t = NULL;
    char *key = NULL;

    if (!aj) return J_FOR_RENT;
    key = xasprintf ("%"PRId64"", j);
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

static int jobid_exist (flux_t h, int64_t j)
{
    kvsdir_t *d;
    if (kvs_get_dir (h, &d, "lwj.%"PRId64"", j) < 0) {
        flux_log (h, LOG_DEBUG, "lwj.%"PRId64" doesn't exist", j);
        return -1;
    }
    kvsdir_destroy (d);
    return 0;
}

static bool fetch_rank_pdesc (JSON src, int64_t *p, int64_t *n, const char **c)
{
    if (!src) return false;
    if (!Jget_str (src, "command", c)) return false;
    if (!Jget_int64 (src, "pid", p)) return false;
    if (!Jget_int64 (src, "nodeid", n)) return false;
    return true;
}

static int build_name_array (zhash_t *ha, const char *k, JSON ns)
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

static int extract_raw_nnodes (flux_t h, int64_t j, int64_t *nnodes)
{
    int rc = 0;
    char *key = xasprintf ("lwj.%"PRId64".nnodes", j);
    if (kvs_get_int64 (h, key, nnodes) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *nnodes);
    free (key);
    return rc;
}

static int extract_raw_ntasks (flux_t h, int64_t j, int64_t *ntasks)
{
    int rc = 0;
    char *key = xasprintf ("lwj.%"PRId64".ntasks", j);
    if (kvs_get_int64 (h, key, ntasks) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *ntasks);
    free (key);
    return rc;
}

static int extract_raw_walltime (flux_t h, int64_t j, int64_t *walltime)
{
    int rc = 0;
    char *key = xasprintf ("lwj.%"PRId64".walltime", j);
    if (kvs_get_int64 (h, key, walltime) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "extract %s: %"PRId64"", key, *walltime);
    return rc;
}

static int extract_raw_rdl (flux_t h, int64_t j, char **rdlstr)
{
    int rc = 0;
    char *key = xasprintf ("lwj.%"PRId64".rdl", j);
    if (kvs_get_string (h, key, rdlstr) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else
        flux_log (h, LOG_DEBUG, "rdl under %s extracted", key);
    free (key);
    return rc;
}

static int extract_raw_state (flux_t h, int64_t j, int64_t *s)
{
    int rc = 0;
    char *key = xasprintf ("lwj.%"PRId64".state", j);
    char *state = NULL;
    if (kvs_get_string (h, key, &state) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else {
        *s = jsc_job_state2num (state);
        flux_log (h, LOG_DEBUG, "extract %s: %s", key, state);
    }
    free (key);
    if (state)
        free (state);
    return rc;
}

static int extract_raw_pdesc (flux_t h, int64_t j, int64_t i, JSON *o)
{
    int rc = 0;
    char *json_str = NULL;
    char *key = xasprintf ("lwj.%"PRId64".%"PRId64".procdesc", j, i);
    if (kvs_get (h, key, &json_str) < 0
            || !(*o = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
        if (json_str)
            free (json_str);
        if (*o)
            Jput (*o);
    }
    free (key);
    return rc;
}

static JSON build_parray_elem (int64_t pid, int64_t eix, int64_t hix)
{
    JSON po = Jnew ();
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_PID, pid);
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_EINDX, eix);
    Jadd_int64 (po, JSC_PDESC_RANK_PDARRAY_HINDX, hix);
    return po;
}

static void add_pdescs_to_jcb (JSON *hns, JSON *ens, JSON *pa, JSON jcb)
{
    json_object_object_add (jcb, JSC_PDESC_HOSTNAMES, *hns);
    json_object_object_add (jcb, JSC_PDESC_EXECS, *ens);
    json_object_object_add (jcb, JSC_PDESC_PDARRAY, *pa);
    /* Because the above transfer ownership, assign NULL should be ok */
    *hns = NULL;
    *ens = NULL;
    *pa = NULL;
}

static int extract_raw_pdescs (flux_t h, int64_t j, int64_t n, JSON jcb)
{
    int rc = -1;
    int64_t i = 0;
    char *hnm;
    const char *cmd = NULL;
    zhash_t *eh = NULL; /* hash holding a set of unique exec_names */
    zhash_t *hh = NULL; /* hash holding a set of unique host_names */
    JSON o = NULL, po = NULL;
    JSON pa = Jnew_ar ();
    JSON hns = Jnew_ar ();
    JSON ens = Jnew_ar ();

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
        hnm = xasprintf ("%"PRId64"", nid);
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

static int extract_raw_rdl_alloc (flux_t h, int64_t j, JSON jcb)
{
    int i = 0;
    char *key;
    int64_t cores = 0;
    JSON ra = Jnew_ar ();
    bool processing = true;

    for (i=0; processing; ++i) {
        key = xasprintf ("lwj.%"PRId64".rank.%"PRId32".cores", j, i);
        if (kvs_get_int64 (h, key, &cores) < 0) {
            if (errno != EINVAL)
                flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
            processing = false;
        } else {
            JSON elem = Jnew ();
            JSON o = Jnew ();
            Jadd_int64 (o, JSC_RDL_ALLOC_CONTAINED_NCORES, cores);
            json_object_object_add (elem, JSC_RDL_ALLOC_CONTAINED, o);
            json_object_array_add (ra, elem);
        }
        free (key);
    }
    json_object_object_add (jcb, JSC_RDL_ALLOC, ra);
    return 0;
}

static int query_jobid (flux_t h, int64_t j, JSON *jcb)
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

static int query_state_pair (flux_t h, int64_t j, JSON *jcb)
{
    JSON o = NULL;
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

static int query_rdesc (flux_t h, int64_t j, JSON *jcb)
{
    JSON o = NULL;
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

static int query_rdl (flux_t h, int64_t j, JSON *jcb)
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

static int query_rdl_alloc (flux_t h, int64_t j, JSON *jcb)
{
    *jcb = Jnew ();
    return extract_raw_rdl_alloc (h, j, *jcb);
}

static int query_pdesc (flux_t h, int64_t j, JSON *jcb)
{
    int64_t ntasks = 0;
    if (extract_raw_ntasks (h, j, &ntasks) < 0) return -1;
    *jcb = Jnew ();
    Jadd_int64 (*jcb, JSC_PDESC_SIZE, ntasks);
    return extract_raw_pdescs (h, j, ntasks, *jcb);
}

static int send_state_event (flux_t h, job_state_t st, int64_t j)
{
    flux_msg_t *msg;
    char *json = NULL;
    char *topic = NULL;
    int rc = -1;

    if ((asprintf (&json, "{\"lwj\":%"PRId64"}", j) < 0)
        || (asprintf (&topic, "jsc.state.%s", jsc_job_num2state (st)) < 0)) {
        flux_log (h, LOG_ERR, "create state change event: %s\n",
                     jsc_job_num2state (st));
        goto done;
    }
    if ((msg = flux_event_encode (topic, json)) == NULL) {
        flux_log (h, LOG_ERR, "flux_event_encode: %s\n", strerror (errno));
        goto done;
    }
    if (flux_send (h, msg, 0) < 0)
        flux_log (h, LOG_ERR, "flux_send event: %s\n", strerror (errno));
    flux_msg_destroy (msg);
    rc = 0;
done:
    free (topic);
    free (json);
    return rc;
}

static int update_state (flux_t h, int64_t j, JSON o)
{
    int rc = -1;
    int64_t st = 0;
    char *key;

    if (!Jget_int64 (o, JSC_STATE_PAIR_NSTATE, &st)) return -1;
    if ((st >= J_FOR_RENT) || (st < J_NULL)) return -1;

    key = xasprintf ("lwj.%"PRId64".state", j);
    if (kvs_put_string (h, key, jsc_job_num2state ((job_state_t)st)) < 0)
        flux_log (h, LOG_ERR, "update %s: %s", key, strerror (errno));
    else if (kvs_commit (h) < 0)
        flux_log (h, LOG_ERR, "commit %s: %s", key, strerror (errno));
    else {
        flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new state: %s", j,
              jsc_job_num2state ((job_state_t)st));
        rc = 0;
    }
    free (key);

    if (send_state_event (h, st, j) < 0)
        flux_log (h, LOG_ERR, "send state event");

    return rc;
}

static int update_rdesc (flux_t h, int64_t j, JSON o)
{
    int rc = -1;
    int64_t nnodes = 0;
    int64_t ntasks = 0;
    int64_t walltime = 0;
    char *key1;
    char *key2;
    char *key3;

    if (!Jget_int64 (o, JSC_RDESC_NNODES, &nnodes)) return -1;
    if (!Jget_int64 (o, JSC_RDESC_NTASKS, &ntasks)) return -1;
    if (!Jget_int64 (o, JSC_RDESC_WALLTIME, &walltime)) return -1;

    if ((nnodes < 0) || (ntasks < 0) || (walltime < 0)) return -1;

    key1 = xasprintf ("lwj.%"PRId64".nnodes", j);
    key2 = xasprintf ("lwj.%"PRId64".ntasks", j);
    key3 = xasprintf ("lwj.%"PRId64".walltime", j);
    if (kvs_put_int64 (h, key1, nnodes) < 0)
        flux_log (h, LOG_ERR, "update %s: %s", key1, strerror (errno));
    else if (kvs_put_int64 (h, key2, ntasks) < 0)
        flux_log (h, LOG_ERR, "update %s: %s", key2, strerror (errno));
    else if (kvs_put_int64 (h, key3, walltime) < 0)
        flux_log (h, LOG_ERR, "update %s: %s", key3, strerror (errno));
    else if (kvs_commit (h) < 0)
        flux_log (h, LOG_ERR, "commit failed");
    else {
        flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new resources.", j);
        rc = 0;
    }
    free (key1);
    free (key2);

    return rc;
}

static int update_rdl (flux_t h, int64_t j, const char *rs)
{
    int rc = -1;
    char *key = xasprintf ("lwj.%"PRId64".rdl", j);
    if (kvs_put_string (h, key, rs) < 0)
        flux_log (h, LOG_ERR, "update %s: %s", key, strerror (errno));
    else if (kvs_commit (h) < 0)
        flux_log (h, LOG_ERR, "commit failed");
    else {
        flux_log (h, LOG_DEBUG, "job (%"PRId64") assigned new rdl.", j);
        rc = 0;
    }
    free (key);

    return rc;
}

static int update_hash_1ra (flux_t h, int64_t j, JSON o, zhash_t *rtab)
{
    int rc = 0;
    int64_t ncores = 0;
    int64_t rank = 0;
    int64_t *curcnt;
    char *key;
    JSON c = NULL;

    if (!Jget_obj (o, JSC_RDL_ALLOC_CONTAINED, &c)) return -1;
    if (!Jget_int64 (c, JSC_RDL_ALLOC_CONTAINING_RANK, &rank)) return -1;
    if (!Jget_int64 (c, JSC_RDL_ALLOC_CONTAINED_NCORES, &ncores)) return -1;

    key = xasprintf ("lwj.%"PRId64".rank.%"PRId64".cores", j, rank);
    if (!(curcnt = zhash_lookup (rtab, key))) {
        curcnt = xzmalloc (sizeof (*curcnt));
        *curcnt = ncores;
        zhash_insert (rtab, key, curcnt);
        zhash_freefn (rtab, key, free);
    } else
        *curcnt = *curcnt + ncores;
    free (key);
    return rc;
}

static int update_rdl_alloc (flux_t h, int64_t j, JSON o)
{
    int i = 0;
    int rc = -1;
    int size = 0;
    JSON ra_e = NULL;
    const char *key = NULL;
    zhash_t *rtab = NULL;
    int64_t *ncores = NULL;

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

    FOREACH_ZHASH (rtab, key, ncores) {
        if ( (rc = kvs_put_int64 (h, key, *ncores)) < 0) {
            flux_log (h, LOG_ERR, "put %s: %s", key, strerror (errno));
            goto done;
        }
    }
    if (kvs_commit (h) < 0) {
        flux_log (h, LOG_ERR, "update_pdesc commit failed");
        goto done;
    }
    rc = 0;

done:
    zhash_destroy (&rtab);
    return rc;
}

static int update_1pdesc (flux_t h, int r, int64_t j, JSON o, JSON ha, JSON ea)
{
    int rc = -1;
    JSON d = NULL;
    char *key;
    char *json_str = NULL;
    const char *hn = NULL, *en = NULL;
    int64_t pid = 0, hindx = 0, eindx = 0, hrank = 0;

    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_PID, &pid)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_HINDX, &hindx)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_EINDX, &eindx)) return -1;
    if (!Jget_ar_str (ha, (int)hindx, &hn)) return -1;
    if (!Jget_ar_str (ea, (int)eindx, &en)) return -1;

    key = xasprintf ("lwj.%"PRId64".%"PRId32".procdesc", j, r);
    if (kvs_get (h, key, &json_str) < 0
            || !(d = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
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
    if (kvs_put (h, key, Jtostr (d)) < 0) {
        flux_log (h, LOG_ERR, "put %s: %s", key, strerror (errno));
        goto done;
    }
    rc = 0;

done:
    free (key);
    if (d)
        Jput (d);
    if (json_str)
        free (json_str);
    return rc;
}

static int update_pdesc (flux_t h, int64_t j, JSON o)
{
    int i = 0;
    int rc = -1;
    int64_t size = 0;
    JSON h_arr = NULL, e_arr = NULL, pd_arr = NULL, pde = NULL;

    if (!Jget_int64 (o, JSC_PDESC_SIZE, &size)) return -1;
    if (!Jget_obj (o, JSC_PDESC_PDARRAY, &pd_arr)) return -1;
    if (!Jget_obj (o, JSC_PDESC_HOSTNAMES, &h_arr)) return -1;
    if (!Jget_obj (o, JSC_PDESC_EXECS, &e_arr)) return -1;

    for (i=0; i < (int) size; ++i) {
        if (!Jget_ar_obj (pd_arr, i, &pde))
            goto done;
        if ( (rc = update_1pdesc (h, i, j, pde, h_arr, e_arr)) < 0)
            goto done;
    }
    if (kvs_commit (h) < 0) {
        flux_log (h, LOG_ERR, "update_pdesc commit failed");
        goto done;
    }
    rc = 0;

done:
    return rc;
}

static JSON get_update_jcb (flux_t h, int64_t j, const char *val)
{
    JSON o = NULL;
    JSON ss = NULL;
    jscctx_t *ctx = getctx (h);
    int64_t ostate = (int64_t) J_FOR_RENT;
    int64_t nstate = (int64_t) J_FOR_RENT;

    nstate = jsc_job_state2num (val);
    if ( (ostate = fetch_and_update_state (ctx->active_jobs, j, nstate)) < 0) {
        flux_log (h, LOG_INFO, "%"PRId64"'s old state unavailable", j);
        ostate = nstate;
    }
    o = Jnew ();
    ss = Jnew ();
    Jadd_int64 (o, JSC_JOBID, j);
    Jadd_int64 (ss, JSC_STATE_PAIR_OSTATE , (int64_t) ostate);
    Jadd_int64 (ss, JSC_STATE_PAIR_NSTATE, (int64_t) nstate);
    json_object_object_add (o, JSC_STATE_PAIR, ss);
    return o;
}


/******************************************************************************
 *                                                                            *
 *                 Internal Asynchronous Notification Mechanisms              *
 *                                                                            *
 ******************************************************************************/

static int invoke_cbs (flux_t h, int64_t j, JSON jcb, int errnum)
{
    int rc = 0;
    cb_pair_t *c = NULL;
    jscctx_t *ctx = getctx (h);
    for (c = zlist_first (ctx->callbacks); c; c = zlist_next (ctx->callbacks)) {
        if (c->cb (jcb, c->arg, errnum) < 0) {
            flux_log (h, LOG_ERR, "callback returns an error");
            rc = -1;
        }
    }
    return rc;
}

static void fixup_newjob_event (flux_t h, int64_t nj)
{
    JSON ss = NULL;
    JSON jcb = NULL;
    int64_t js = J_NULL;
    char *key = xasprintf ("%"PRId64"", nj);
    jscctx_t *ctx = getctx (h);

    /* We fix up ordering problem only when new job
       event hasn't been reported through a kvs watch
     */
    jcb = Jnew ();
    ss = Jnew ();
    Jadd_int64 (jcb, JSC_JOBID, nj);
    Jadd_int64 (ss, JSC_STATE_PAIR_OSTATE , (int64_t) js);
    Jadd_int64 (ss, JSC_STATE_PAIR_NSTATE, (int64_t) js);
    json_object_object_add (jcb, JSC_STATE_PAIR, ss);
    if (zhash_insert (ctx->active_jobs, key, (void *)(intptr_t)js) < 0) {
        flux_log (h, LOG_ERR, "new_job_cb: inserting a job to hash failed");
        goto done;
    }
    if (invoke_cbs (h, nj, jcb, 0) < 0) {
        flux_log (h, LOG_ERR,
                     "makeup_newjob_event: failed to invoke callbacks");
        goto done;
    }
done:
    free (key);
    return;
}

static inline void delete_jobinfo (flux_t h, int64_t jobid)
{
    jscctx_t *ctx = getctx (h);
    char *key = xasprintf ("%"PRId64"", jobid);
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

static void job_state_cb (flux_t h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    int64_t jobid = -1;
    json_object *o = NULL;
    const char *topic = NULL;
    const char *json_str = NULL;
    const char *state = NULL;
    int len = 12;

    if (flux_msg_get_topic (msg, &topic) < 0)
        goto done;

    if (flux_event_decode (msg, NULL, &json_str) < 0
            || !(o = Jfromstr (json_str))
            || Jget_int64 (o, "lwj", &jobid) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }

    if (strncmp (topic, "jsc", 3) == 0)
       len = 10;

    state = topic + len;
    if (strcmp (state, jsc_job_num2state (J_RESERVED)) == 0)
        fixup_newjob_event (h, jobid);

    if (invoke_cbs (h, jobid, get_update_jcb (h, jobid, state), 0) < 0)
        flux_log (h, LOG_ERR, "job_state_cb: failed to invoke callbacks");

    if (job_is_finished (state))
        delete_jobinfo (h, jobid);
done:
    return;
}

/******************************************************************************
 *                                                                            *
 *                    Public Job Status and Control API                       *
 *                                                                            *
 ******************************************************************************/
static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,     "wreck.state.*", job_state_cb},
    { FLUX_MSGTYPE_EVENT,     "jsc.state.*",   job_state_cb},
      FLUX_MSGHANDLER_TABLE_END
};

int jsc_notify_status_obj (flux_t h, jsc_handler_obj_f func, void *d)
{
    int rc = -1;
    cb_pair_t *c = NULL;
    jscctx_t *ctx = NULL;

    if (!func)
        goto done;
    if (flux_event_subscribe (h, "wreck.state.") < 0) {
        flux_log (h, LOG_ERR, "subscribing to job event: %s",
                     strerror (errno));
        rc = -1;
        goto done;
    }
    if (flux_event_subscribe (h, "jsc.state.") < 0) {
        flux_log (h, LOG_ERR, "subscribing to job event: %s",
                     strerror (errno));
        rc = -1;
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, (void *)ctx) < 0) {
        flux_log (h, LOG_ERR, "registering resource event handler: %s",
                  strerror (errno));
        rc = -1;
        goto done;
    }

    ctx = getctx (h);
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

struct callback_wrapper {
    jsc_handler_f cb;
    void *arg;
};

static int wrap_handler (json_object *base_jcb, void *arg, int errnum)
{
    struct callback_wrapper *wrap = arg;
    return wrap->cb (Jtostr (base_jcb), wrap->arg, errnum);
}

int jsc_notify_status (flux_t h, jsc_handler_f func, void *d)
{
    int rc = -1;
    struct callback_wrapper *wrap = xzmalloc (sizeof (*wrap));

    wrap->cb = func;
    wrap->arg = d;

    rc = jsc_notify_status_obj (h, wrap_handler, wrap);
    if (rc < 0)
        free (wrap);
    return rc;
}

int jsc_query_jcb_obj (flux_t h, int64_t jobid, const char *key, JSON *jcb)
{
    int rc = -1;

    if (!key) return -1;
    if (jobid_exist (h, jobid) != 0) return -1;

    if (is_jobid (key)) {
        if ( (rc = query_jobid (h, jobid, jcb)) < 0)
            flux_log (h, LOG_ERR, "query_jobid failed");
    } else if (is_state_pair (key)) {
        if ( (rc = query_state_pair (h, jobid, jcb)) < 0)
            flux_log (h, LOG_ERR, "query_pdesc failed");
    } else if (is_rdesc (key)) {
        if ( (rc = query_rdesc (h, jobid, jcb)) < 0)
            flux_log (h, LOG_ERR, "query_rdesc failed");
    } else if (is_rdl (key)) {
        if ( (rc = query_rdl (h, jobid, jcb)) < 0)
            flux_log (h, LOG_ERR, "query_rdl failed");
    } else if (is_rdl_alloc (key)) {
        if ( (rc = query_rdl_alloc (h, jobid, jcb)) < 0)
            flux_log (h, LOG_ERR, "query_rdl_alloc failed");
    } else if (is_pdesc (key)) {
        if ( (rc = query_pdesc (h, jobid, jcb)) < 0)
            flux_log (h, LOG_ERR, "query_pdesc failed");
    } else
        flux_log (h, LOG_ERR, "key (%s) not understood", key);

    return rc;
}

int jsc_query_jcb (flux_t h, int64_t jobid, const char *key, char **jcb)
{
    int rc;
    JSON o = NULL;

    rc = jsc_query_jcb_obj (h, jobid, key, &o);
    if (rc < 0)
        goto done;
    *jcb = o ? xstrdup (Jtostr (o)) : NULL;
done:
    Jput (o);
    return rc;
}

int jsc_update_jcb_obj (flux_t h, int64_t jobid, const char *key, JSON jcb)
{
    int rc = -1;
    JSON o = NULL;

    if (!jcb) return -1;
    if (jobid_exist (h, jobid) != 0) return -1;

    if (is_jobid (key)) {
        flux_log (h, LOG_ERR, "jobid attr cannot be updated");
    } else if (is_state_pair (key)) {
        if (Jget_obj (jcb, JSC_STATE_PAIR, &o))
            rc = update_state (h, jobid, o);
    } else if (is_rdesc (key)) {
        if (Jget_obj (jcb, JSC_RDESC, &o))
            rc = update_rdesc (h, jobid, o);
    } else if (is_rdl (key)) {
        const char *s = NULL;
        if (Jget_str (jcb, JSC_RDL, &s))
            rc = update_rdl (h, jobid, s);
    } else if (is_rdl_alloc (key)) {
        if (Jget_obj (jcb, JSC_RDL_ALLOC, &o))
            rc = update_rdl_alloc (h, jobid, o);
    } else if (is_pdesc (key)) {
        if (Jget_obj (jcb, JSC_PDESC, &o))
            rc = update_pdesc (h, jobid, o);
    }
    else
        flux_log (h, LOG_ERR, "key (%s) not understood", key);

    return rc;
}

int jsc_update_jcb (flux_t h, int64_t jobid, const char *key, const char *jcb)
{
    int rc = -1;
    JSON o = NULL;

    if (!jcb || !(o = Jfromstr (jcb))) {
        errno = EINVAL;
        goto done;
    }
    rc = jsc_update_jcb_obj (h, jobid, key, o);
done:
    Jput (o);
    return rc;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
