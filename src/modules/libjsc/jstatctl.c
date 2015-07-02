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
#include <flux/core.h>

#include "jstatctl.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
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
    char key[20] = {'\0'};        
    
    if (!aj) return J_FOR_RENT;;    
    snprintf (key, 20, "%ld", j);
    if ( !(t = ((int *)zhash_lookup (aj, (const char *)key)))) 
        return J_FOR_RENT;
    if (ns == J_COMPLETE) 
        zhash_delete (aj, key);
    else
        zhash_update (aj, key, (void *)(intptr_t)ns);

    /* safe to convert t to int */
    return (intptr_t) t;
} 


/******************************************************************************
 *                                                                            *
 *                         Internal JCB Accessors                             *
 *                                                                            *
 ******************************************************************************/

static int parse_jobid (const char *k, int64_t *i)
{
    int rc = -1;
    char *kcopy = NULL, *sptr = NULL, *j = NULL, *id = NULL;  

    kcopy = xstrdup (k);
    j = strtok_r (kcopy, ".", &sptr);
    if (strncmp(j, "lwj", 3) != 0) 
        goto done;

    id = strtok_r (NULL, ".", &sptr);
    errno  = 0;
    *i = strtoul(id, (char **) NULL, 10);
    if (errno != 0)
        goto done;
    rc = 0;

done:
    free (kcopy);
    return rc;
}

static int jobid_exist (flux_t h, int64_t j)
{
    kvsdir_t d;
    if (kvs_get_dir (h, &d, "lwj.%ld", j) < 0) {
        flux_log (h, LOG_DEBUG, "lwj.%ld doesn't exist", j);
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
    char key[20] = {'\0'};
    snprintf (key, 20, "lwj.%ld.nnodes", j);
    if (kvs_get_int64 (h, key, nnodes) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else 
        flux_log (h, LOG_DEBUG, "extract %s: %ld", key, *nnodes);
    return rc;
}

static int extract_raw_ntasks (flux_t h, int64_t j, int64_t *ntasks)
{
    int rc = 0;
    char key[20] = {'\0'};
    snprintf (key, 20, "lwj.%ld.ntasks", j);
    if (kvs_get_int64 (h, key, ntasks) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else 
        flux_log (h, LOG_DEBUG, "extract %s: %ld", key, *ntasks);
    return rc;
}

static int extract_raw_rdl (flux_t h, int64_t j, char **rdlstr)
{
    int rc = 0;
    char key[20] = {'\0'};
    snprintf (key, 20, "lwj.%ld.rdl", j);
    if (kvs_get_string (h, key, rdlstr) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else 
        flux_log (h, LOG_DEBUG, "rdl under %s extracted", key);
    return rc;
}

static int extract_raw_state (flux_t h, int64_t j, int64_t *s)
{
    int rc = 0;
    char key[20] = {'\0'};
    char *state = NULL;
    snprintf (key, 20, "lwj.%ld.state", j);
    if (kvs_get_string (h, key, &state) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
    }
    else {
        *s = jsc_job_state2num (state);
        flux_log (h, LOG_DEBUG, "extract %s: %s", key, state);
    }
    if (state)
        free (state);
    return rc;
}

static int extract_raw_pdesc (flux_t h, int64_t j, int64_t i, JSON *o)
{
    int rc = 0;
    char key[20] = {'\0'}; 
    snprintf (key, 20, "lwj.%ld.%ld.procdesc", j, i);
    if (kvs_get (h, key, o) < 0) {
        flux_log (h, LOG_ERR, "extract %s: %s", key, strerror (errno));
        rc = -1;
        if (*o) 
            Jput (*o);
    }
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
    char hnm[20] = {'\0'};
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
        snprintf (hnm, 20, "%ld", nid); 
        hix = build_name_array (hh, hnm, hns);
        po = build_parray_elem (pid, eix, hix); 
        json_object_array_add (pa, po);
        po = NULL;
        Jput (o);
        o = NULL;
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
    char k[20];
    int64_t cores = 0;
    JSON ra = Jnew_ar ();
    bool processing = true;

    for (i=0; processing; ++i) {
        snprintf (k, 20, "lwj.%ld.rank.%d.cores", j, i);
        if (kvs_get_int64 (h, k, &cores) < 0) {
            if (errno != EINVAL) 
                flux_log (h, LOG_ERR, "extract %s: %s", k, strerror (errno));
            processing = false; 
        } else {
            JSON elem = Jnew ();       
            JSON o = Jnew ();
            Jadd_int64 (o, JSC_RDL_ALLOC_CONTAINED_NCORES, cores);
            json_object_object_add (elem, JSC_RDL_ALLOC_CONTAINED, o);
            json_object_array_add (ra, elem);
        }
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
    
    if (extract_raw_nnodes (h, j, &nnodes) < 0) return -1;
    if (extract_raw_ntasks (h, j, &ntasks) < 0) return -1;

    *jcb = Jnew ();
    o = Jnew ();
    Jadd_int64 (o, JSC_RDESC_NNODES, nnodes);
    Jadd_int64 (o, JSC_RDESC_NTASKS, ntasks);
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

static int update_state (flux_t h, int64_t j, JSON o)
{
    int rc = -1;
    int64_t st = 0;
    char key[20] = {'\0'}; 

    if (!Jget_int64 (o, JSC_STATE_PAIR_NSTATE, &st)) return -1;
    if ((st >= J_FOR_RENT) || (st < J_NULL)) return -1; 

    snprintf (key, 20, "lwj.%ld.state", j);
    if (kvs_put_string (h, key, jsc_job_num2state ((job_state_t)st)) < 0) 
        flux_log (h, LOG_ERR, "update %s: %s", key, strerror (errno));
    else if (kvs_commit (h) < 0) 
        flux_log (h, LOG_ERR, "commit %s: %s", key, strerror (errno));
    else {
        flux_log (h, LOG_DEBUG, "job (%ld) assigned new state: %s", j, 
              jsc_job_num2state ((job_state_t)st));
        rc = 0;
    }

    return rc;
}

static int update_rdesc (flux_t h, int64_t j, JSON o)
{
    int rc = -1;
    int64_t nnodes = 0;
    int64_t ntasks = 0;
    char key1[20] = {'\0'}; 
    char key2[20] = {'\0'}; 

    if (!Jget_int64 (o, JSC_RDESC_NNODES, &nnodes)) return -1;
    if (!Jget_int64 (o, JSC_RDESC_NTASKS, &ntasks)) return -1;
    if ((nnodes < 0) || (ntasks < 0)) return -1;

    snprintf (key1, 20, "lwj.%ld.nnodes", j);
    snprintf (key2, 20, "lwj.%ld.ntasks", j);
    if (kvs_put_int64 (h, key1, nnodes) < 0) 
        flux_log (h, LOG_ERR, "update %s: %s", key1, strerror (errno));
    else if (kvs_put_int64 (h, key2, ntasks) < 0) 
        flux_log (h, LOG_ERR, "update %s: %s", key2, strerror (errno));
    else if (kvs_commit (h) < 0) 
        flux_log (h, LOG_ERR, "commit failed");
    else {
        flux_log (h, LOG_DEBUG, "job (%ld) assigned new resources.", j);
        rc = 0;
    }

    return rc;
}

static int update_rdl (flux_t h, int64_t j, const char *rs)
{
    int rc = -1;
    char key[20] = {'\0'}; 

    snprintf (key, 20, "lwj.%ld.rdl", j);
    if (kvs_put_string (h, key, rs) < 0) 
        flux_log (h, LOG_ERR, "update %s: %s", key, strerror (errno));
    else if (kvs_commit (h) < 0) 
        flux_log (h, LOG_ERR, "commit failed");
    else {
        flux_log (h, LOG_DEBUG, "job (%ld) assigned new rdl.", j);
        rc = 0;
    }

    return rc;
}

static int update_1ra (flux_t h, int r, int64_t j, JSON o)
{
    int rc = 0;
    int64_t ncores = 0;
    char key[20] = {'\0'};
    JSON c = NULL;

    if (!Jget_obj (o, JSC_RDL_ALLOC_CONTAINED, &c)) return -1;
    if (!Jget_int64 (c, JSC_RDL_ALLOC_CONTAINED_NCORES, &ncores)) return -1;

    snprintf (key, 20, "lwj.%ld.rank.%d.cores", j, r);
    if ( (rc = kvs_put_int64 (h, key, ncores)) < 0) {
        flux_log (h, LOG_ERR, "put %s: %s", key, strerror (errno));
    }  
    return rc;
}

static int update_rdl_alloc (flux_t h, int64_t j, JSON o) 
{
    int i = 0;
    int rc = -1;
    int size = 0;
    JSON ra_e = NULL;

    if (!Jget_ar_len (o, &size)) return -1;

    for (i=0; i < (int) size; ++i) {
        if (!Jget_ar_obj (o, i, &ra_e))
            goto done;
        if ( (rc = update_1ra (h, i, j, ra_e)) < 0)
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

static int update_1pdesc (flux_t h, int r, int64_t j, JSON o, JSON ha, JSON ea)
{
    int rc = -1;
    JSON d = NULL;
    char key[20] = {'\0'};;
    const char *hn = NULL, *en = NULL;
    int64_t pid = 0, hindx = 0, eindx = 0, hrank = 0;

    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_PID, &pid)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_HINDX, &hindx)) return -1;
    if (!Jget_int64 (o, JSC_PDESC_RANK_PDARRAY_EINDX, &eindx)) return -1;
    if (!Jget_ar_str (ha, (int)hindx, &hn)) return -1;
    if (!Jget_ar_str (ea, (int)eindx, &en)) return -1;

    snprintf (key, 20, "lwj.%ld.%d.procdesc", j, r);
    if (kvs_get (h, key, &d) < 0) {
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
    if (kvs_put (h, key, d) < 0) {
        flux_log (h, LOG_ERR, "put %s: %s", key, strerror (errno));
        goto done;
    }
    rc = 0;

done:
    if (d)
        Jput (d);        
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

static inline int chk_errnum (flux_t h, int errnum)
{
    if (errnum > 0) {
        /* Ignore ENOENT.  It is expected when this cb is called right
         * after registration.
         */
        if (errnum != ENOENT) {
            flux_log (h, LOG_ERR, "in a callback  %s", strerror (errnum));
        }
        return -1;
    }
    return 0;
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
        flux_log (h, LOG_INFO, "%ld's old state unavailable", j);
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

static int job_state_cb (const char *key, const char *val, void *arg, int errnum)
{
    int64_t jobid = -1;
    flux_t h = (flux_t) arg;

    if (chk_errnum (h, errnum) < 0) 
        flux_log (h, LOG_ERR, "job_state_cb: key(%s), val(%s)", key, val);
    else if (parse_jobid (key, &jobid) != 0) 
        flux_log (h, LOG_ERR, "job_state_cb: key ill-formed");
    else if (invoke_cbs (h, jobid, get_update_jcb (h, jobid, val), errnum) < 0) 
        flux_log (h, LOG_ERR, "job_state_cb: failed to invoke callbacks");

    /* always return 0 so that reactor will not return */
    return 0;
}

static int reg_jobstate_hdlr (flux_t h, const char *path, KVSSetStringF *func)
{
    int rc = 0;
    char key[20] = {'\0'};

    snprintf (key, 20, "%s.state", path);
    if (kvs_watch_string (h, key, func, (void *)h) < 0) {
        flux_log (h, LOG_ERR, "watch %s: %s.", key, strerror (errno));
        rc = -1;
    } else
        flux_log (h, LOG_DEBUG, "registered job %s.state CB", path);
    return rc;
}

static int new_job_cb (const char *key, int64_t val, void *arg, int errnum)
{
    int64_t nj = 0;
    int64_t js = 0;
    JSON ss = NULL;
    JSON jcb = NULL;
    char k[20] = {'\0'};
    char path[20] = {'\0'};
    flux_t h = (flux_t) arg;
    jscctx_t *ctx = getctx (h);

    if (ctx->first_time == 1) {
        /* watch is invoked immediately and we shouldn't
         * rely on that event at all.
         */
        ctx->first_time = 0;
        return 0;
    }

    if (chk_errnum (h, errnum) < 0) return 0;

    flux_log (h, LOG_DEBUG, "new_job_cb invoked: key(%s), val(%ld)", key, val);

    js = J_NULL;
    nj = val-1;
    snprintf (k, 20, "%ld", nj);
    snprintf (path, 20, "lwj.%ld", nj);
    if (zhash_insert (ctx->active_jobs, k, (void *)(intptr_t)js) < 0) {
        flux_log (h, LOG_ERR, "new_job_cb: inserting a job to hash failed");
        goto done;
    }

    flux_log (h, LOG_DEBUG, "jobstate_hdlr registered");
    jcb = Jnew ();
    ss = Jnew ();
    Jadd_int64 (jcb, JSC_JOBID, nj);
    Jadd_int64 (ss, JSC_STATE_PAIR_OSTATE , (int64_t) js);
    Jadd_int64 (ss, JSC_STATE_PAIR_NSTATE, (int64_t) js);
    json_object_object_add (jcb, JSC_STATE_PAIR, ss);

    if (invoke_cbs (h, nj, jcb, errnum) < 0) {
        flux_log (h, LOG_ERR, "new_job_cb: failed to invoke callbacks");
    }
    if (reg_jobstate_hdlr (h, path, (KVSSetStringF *) job_state_cb) == -1) {
        flux_log (h, LOG_ERR, "new_job_cb: reg_jobstate_hdlr: %s", 
            strerror (errno));
    }

done:
    /* always return 0 so that reactor won't return */
    return 0;
}

static int reg_newjob_hdlr (flux_t h, KVSSetInt64F *func)
{
    if (kvs_watch_int64 (h,"lwj.next-id", func, (void *) h) < 0) {
        flux_log (h, LOG_ERR, "watch lwj.next-id: %s", strerror (errno));
        return -1;
    }
    flux_log (h, LOG_DEBUG, "registered job creation CB");
    return 0;
}


/******************************************************************************
 *                                                                            *
 *                    Public Job Status and Control API                       *
 *                                                                            *
 ******************************************************************************/

int jsc_notify_status (flux_t h, jsc_handler_f func, void *d)
{
    int rc = -1;
    cb_pair_t *c = NULL; 
    jscctx_t *ctx = NULL; 

    if (!func) 
        goto done;
    if (reg_newjob_hdlr (h, (KVSSetInt64F*)new_job_cb) == -1) {
        flux_log (h, LOG_ERR, "jsc_notify_status: reg_newjob_hdlr failed");
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

int jsc_query_jcb (flux_t h, int64_t jobid, const char *key, JSON *jcb)
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

int jsc_update_jcb (flux_t h, int64_t jobid, const char *key, JSON jcb)
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


/*
 * vi: ts=4 sw=4 expandtab
 */
