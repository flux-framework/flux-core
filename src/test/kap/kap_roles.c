/* producer code */

#include <stdio.h>
#include <stdlib.h>
#include <flux/core.h>
#include "src/common/libcompat/compat.h"
#include "src/modules/kvs/kvs_deprecated.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/base64_json.h"
#include "kap.h"

#define VAL_UNIT_SIZE    8 
#define VAL_MULTIPLE_CHK 0xfffffff8 

#define KVS_INST_DIR    "inst"
#define KVS_BASE_DIR    "kap"
#define KVS_SHARD_DIR   "dir"

/*********************************************************
 *                  STATIC FUNCTIONS                     *
 *                                                       *
 *********************************************************/

/*  
    This isn't necessary but to make our testing more
    lock-step fashion.

    Base KVS dir structure 
    /kap.0/dir.0/producer.0 /kap.0/producer.1 ... 

    If --ndirs=2 is given,
    /inst.0/kap.0/dir.0/producer.0 ... producer.(size/2-1)
    /inst.0/kap.0/dir.1/producer.(size/2) ... producer.(size-1)

    In general --ndirs=N is given, 
    more subdirectories are created to spread the 
    producers' objects. 
*/
static int
create_dir_struct (kap_params_t *param)
{
    int i, j;
    char d[KAP_MAX_STR];

    if (param->pers.rank != 0) 
        return 0;

    for (i=0; i < param->config.iter_producer; ++i) {
        for (j=0; j < param->config.ndirs; ++j) {
            snprintf (d, KAP_MAX_STR, "/%s.%d/%s.%d/%s.%d", 
                KVS_INST_DIR, (int)param->config.instance_num,
                KVS_BASE_DIR, i,
                KVS_SHARD_DIR, j);

            if (kvs_mkdir (param->pers.handle, d) < 0) {
                fprintf (stderr, 
                    "kvs_mkdir returned an error: %s.\n", d);
                return -1;
            }
        }
    }

    return 0;
}


static int 
gen_val (kap_params_t *param, uint64_t **dat, int *len)
{
    uint64_t s; 
    uint64_t *chunk;
    int chunk_size;
    int i;

    if (param->config.value_size < VAL_UNIT_SIZE) {
        fprintf (stderr, 
            "Producer write size is smaller than %d.\n",
            VAL_UNIT_SIZE);
        return -1; 
    }

    param->config.value_size &= VAL_MULTIPLE_CHK;
    chunk_size = param->config.value_size / VAL_UNIT_SIZE;

    s = param->pers.rank * chunk_size; 
    s += param->pers.size * param->pers.iter_count;

    chunk = (uint64_t *) malloc (chunk_size * sizeof(uint64_t));

    if (param->config.redundant_val == 0) {
        for (i=0; i < chunk_size; ++i) {
            chunk[i] = s;
            s++;    
        }
    }
    else {
        for (i=0; i < chunk_size; ++i) {
            chunk[i] = i;
            s++;    
        }
    }

    *dat = chunk; 
    *len = chunk_size;

    return 0;
}


static int
fqkey (kap_params_t *param, int rank, char* buf, int len)
{
    snprintf (buf, len,
              "/%s.%d/%s.%d/%s.%d/producer.%d",
              KVS_INST_DIR,
              (int) param->config.instance_num,
              KVS_BASE_DIR, 
              param->pers.iter_count,
              KVS_SHARD_DIR, 
              //rank % param->config.ndirs,
              rank / (param->pers.size / param->config.ndirs),
              rank);

    return 0;
}


static int
gen_fqkey (kap_params_t *param, char* buf, int len)
{
    return (fqkey (param, param->pers.rank, buf, len)); 
}


static int
find_fqkey (kap_params_t *param, 
                int fet_i, char *buf, int len)
{
    int t_rank;
    int remap;

    /* map my rank to the producer rank for the case
       where nconsumers > nproducers */
    t_rank = param->pers.rank - (param->pers.size - param->config.nconsumers);
    remap = param->config.nconsumers / param->config.nproducers;
    if (remap > 0) {
        t_rank = t_rank / remap;
    }

    t_rank = ((t_rank 
              + (param->config.access_stride * fet_i))
                % param->config.nproducers);

#if 0
    if (t_rank == param->pers.rank) {
        fprintf (stdout, "Warning: target rank == self "
            "(Rank: %d) (Stride: %d) (Fetch count: %d)\n",
            t_rank, 
            param->config.access_stride,
            fet_i);
    }
#endif

    return (fqkey (param, t_rank, buf, len)); 
}


static int
fetch_kv_tuple (kap_params_t *param, int fet_i, 
                int64_t **b, int *b_len)
{
    char k[KAP_MAX_STR];
    json_object *o = NULL;

    if ( find_fqkey (param, fet_i, k, KAP_MAX_STR) < 0 ) {
        fprintf (stderr,
            "Can't find the producer's FQ key.\n");
        goto error;
    }

   /***********************************************
    *            MEASURE Gets LATENCY             *
    **********************************************/
    Begin = now ();
    if ( kvs_get_obj (param->pers.handle, k, &o) < 0) {
        fprintf (stderr, "kvs_get failed.\n");
        goto error;
    }
    End = now ();
    update_metric (&Gets, Begin, End);
    if ( base64_json_decode (Jobj_get (o, KAP_VAL_NAME),
                             (uint8_t **) b, b_len) < 0 ) {
        fprintf (stderr, "get JSON failed.\n");
        goto error;
    }

    return 0;

error:
    return -1;
}


static int 
commit_kv_cache (kap_params_t *param)
{
    if ( param->config.iter_commit 
         && !((param->pers.iter_count+1) 
              % param->config.iter_commit)) {
        char fence_n[KAP_MAX_STR];

        switch (param->config.iter_commit_type) {
        case s_fence:
            snprintf (fence_n, KAP_MAX_STR,
                "iter-%d-fen-%d", 
                param->pers.iter_count,
                (int) param->config.instance_num);
            
            /***********************************************
             *        MEASURE Commit_bn_Puts LATENCY       *
             **********************************************/
            Begin = now ();
            if ( kvs_fence (param->pers.handle,
                            fence_n,
                            param->config.nproducers) < 0) {
                fprintf (stderr, "kvs_commit failed.\n\n");
                    goto error;
                }
            End = now ();
            update_metric (&Commit_bn_Puts, Begin, End);
            break;
        case s_commit:
            /***********************************************
             *        MEASURE Commit_bn_Puts LATENCY       *
             **********************************************/
            Begin = now ();
            if ( kvs_commit (param->pers.handle) < 0) {
                fprintf (stderr, "kvs_commit failed.\n\n");
                goto error;
            }
            End = now ();
            update_metric (&Commit_bn_Puts, Begin, End);
            break;
        default:
            fprintf (stderr,
                "Unknown commit mechanism: no commit.\n");
            break;
        }
    }

    return 0;

error:
    return -1;
}


static int
put_test_obj (kap_params_t *param)
{
    int len = 0;
    uint64_t *dat = NULL;
    json_object *o = NULL;
    char k[KAP_MAX_STR];

    if ( (gen_fqkey (param, k, KAP_MAX_STR) == 0)
        && (gen_val (param, &dat, &len) == 0)) {
            if (!(o = json_object_new_object ())) {
                fprintf (stderr, 
                    "OOM: json_object_new_object\n");
                goto error;
            }

            json_object_object_add (o, KAP_VAL_NAME,
                base64_json_encode ((uint8_t *)dat, sizeof(uint64_t)*len));
        
            /***********************************************
             *        MEASURE PUT LATENCY                  *
             **********************************************/
            Begin = now ();
            if ( kvs_put_obj (param->pers.handle, k, o) < 0) {
                fprintf (stderr, 
                    "kvs_put failed %s", flux_strerror(errno));
                goto error;
            }
            End = now ();
            update_metric (&Puts, Begin, End);

            free (dat);
            json_object_put (o);
    }
    else {
        fprintf (stderr, 
            "Failed to generate a tuple.\n");
        goto error;
    }

    return 0;

error:
    return -1;
}


static int
run_fence_sync (kap_params_t *param)
{
    int fence_count;
    int actor_count;
    actor_count = param->config.nproducers
                  + param->config.nconsumers;

    fence_count= (actor_count < param->config.total_num_proc) 
                    ? actor_count 
                    : param->config.total_num_proc;

    if ( param->pers.role != k_none ) {
        char fn[KAP_MAX_STR];
        snprintf (fn, KAP_MAX_STR, 
                  "pr-co-fen-%d",
                  (int) param->config.instance_num);   
        if ( kvs_fence (param->pers.handle, 
                       fn, 
                       fence_count)  < 0) {
            fprintf (stderr,
                "kvs_fence failed.\n");
            goto error;
        }
    }

    return 0;

error:
    return -1;
}


static int
send_causal_event (kap_params_t *param)
{
    int v = 0;
    json_object *o = NULL;

    if (kvs_get_version (param->pers.handle, &v)) {
        fprintf (stderr,
            "kvs_fence failed.\n");
            goto error;
    }

    o = json_object_new_object (); 
    Jadd_int (o, KAP_KVSVER_NAME, v);

    flux_msg_t * msg = flux_event_encode(KAP_CAUSAL_CONS_EV, Jtostr(o));
    if (flux_sendmsg (param->pers.handle, &msg) < 0) {
        fprintf (stderr,
            "flux_event_send failed.\n");
            goto error;
    }

    return 0;

error:
    return -1;
}


static int
enforce_c_consistency (kap_params_t *param)
{
    const char *tag = NULL;
    int v = 0;
    json_object *o = NULL;

    flux_msg_t * msg = flux_recv (param->pers.handle, FLUX_MATCH_EVENT, 0);
    if ( ! msg ) {
        fprintf (stderr,
            "event recv failed: %s\n", flux_strerror (errno));
        goto error;
    }
    flux_msg_get_topic (msg, &tag);
    if ( strcmp (tag, KAP_CAUSAL_CONS_EV) != 0) {
        fprintf (stderr,
            "Unexpected tag.\n");
        goto error;
    }
    const char *json_str;
    flux_msg_get_json (msg, &json_str);
    o = json_tokener_parse (json_str);
    if ( !Jget_int (o, KAP_KVSVER_NAME, &v)) {
        fprintf (stderr,
            "json_object_get_int failed.\n");
        goto error;
    }  
    if ( kvs_wait_version (param->pers.handle, v) < 0) {
        fprintf (stderr,
            "flux_wait_version failed.\n");
        goto error;
    }

    json_object_put (o);

    return 0;

error:
    return -1;
}


static int
run_causal_sync (kap_params_t *param)
{
    if ( IS_PRODUCER(param->pers.role)) {
        char fn[KAP_MAX_STR];
        snprintf (fn, KAP_MAX_STR,
                  "pr-causal-fen-%d",
                  (int) param->config.instance_num);
        if ( kvs_fence (param->pers.handle, 
                       fn, 
                       param->config.nproducers) < 0) {
            fprintf (stderr,
                "kvs_fence failed.\n");
            goto error;
        }
    }
    if ( (param->pers.rank == 0)
         && IS_PRODUCER(param->pers.role) ) {

        if (send_causal_event (param) < 0) {
            fprintf (stderr,
                "send_causal_event return an error.\n");
            goto error;
        }
    }
    else if ( param->pers.rank == 0 ){
        fprintf (stderr,
            "Rank 0 is not a producer!\n");
        goto error;
    }

    if ( IS_CONSUMER(param->pers.role)) { 
        if ( enforce_c_consistency (param) < 0 ) {
            fprintf (stderr,
                "Enforcing causal consistency failed.\n");
            goto error;
        }
    }

    return 0;

error:
    return -1;
}


/*********************************************************
 *                  PUBLIC FUNCTIONS                     *
 *                                                       *
 *********************************************************/
int
run_producer (kap_params_t *param)
{
    char fn[KAP_MAX_STR];

    if ( !IS_PRODUCER(param->pers.role) ) {
        fprintf (stderr, 
            "Only producers should call kap_producer.\n");       
        goto error;
    }

    if (param->pers.rank == 0) {
        if ( create_dir_struct (param) < 0) {
            goto error; 
        }
    }

    /* This fence will not work if you run the KAP tester 
     * multiple times within the same CMB session.
     */
    snprintf (fn, KAP_MAX_STR,
        "prod-st-fen-%d",
        (int) param->config.instance_num);
    if ( kvs_fence (param->pers.handle, fn, 
                     param->config.nproducers) < 0) {
        fprintf (stderr, 
            "flux_fence (%s) failed.\n", fn);       
        goto error;
    }

    for ( param->pers.iter_count=0; 
            ( param->pers.iter_count < param->config.iter_producer ); 
                ( param->pers.iter_count++ ) ) {

       /* 
        * Simultaneous PUTs issued by producers 
        */                                    
        if ( put_test_obj (param) < 0) {
            fprintf (stderr,
                "put test routine returned an error.\n");
            goto error;
        }

       /* 
        * Commit KV cache for this iteration
        */
        if ( commit_kv_cache (param) < 0) {
            fprintf (stderr,
                "kv commit routine returned an error.\n");
            goto error;
        }
    }

    return 0;

error:
    return -1;
}


int
run_consumer (kap_params_t *param)
{
    int i, ac;
    if ( !IS_CONSUMER(param->pers.role) ) {
        fprintf (stderr, 
            "Only consumers should call run_consumer.\n");       
        goto error;
    }

    for ( param->pers.iter_count=0; 
            ( param->pers.iter_count < param->config.iter_consumer ); 
                ( param->pers.iter_count++ ) ) {

        int64_t *vb = NULL;
        int vb_len = 0;
        ac = param->config.consumer_access_count;
        for ( i=0; i < ac; i++ ) {
            if ( fetch_kv_tuple (param, (i+1), &vb, &vb_len) < 0) {
                fprintf (stderr, 
                    "Fail fetching a tuple.\n");       
                goto error;
            }
            if (vb_len != param->config.value_size) {
                fprintf (stderr, 
                    "Value size mismatch.\n");       
                goto error;
            }
            free (vb);
        }
    }

    return 0;

error:
    return -1;
}


int 
sync_prod_and_cons (kap_params_t *param)
{
    int rc = 0;

    switch (param->config.sync_type) {
    case s_fence:
        /***********************************************
         *     MEASURE fence-based SYNC LATENCY        *
         **********************************************/
        Begin = now ();
        if ( (rc = run_fence_sync (param)) < 0) {
            fprintf (stderr,
                "run_fence_sync failed.\n");  
        }
        End = now ();
        update_metric (&Sync_bn_Puts_Gets, Begin, End);
        break;
    case s_causal:
        /***********************************************
         *MEASURE causal consistency-based SYNC LATENCY*
         **********************************************/
        Begin = now ();
        if ( (rc = run_causal_sync (param)) < 0) {
            fprintf (stderr,
                "run_causal_sync failed.\n");   
        End = now ();
        update_metric (&Sync_bn_Puts_Gets, Begin, End);
        }
        break;
    default:
        fprintf (stderr,
            "sync_type not supported.\n");   
        break;   
    }

    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

