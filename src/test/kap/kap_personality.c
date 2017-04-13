/*
 * KAP init and process personality 
 *    -- this may need to be adjusted, not to use MPI later
 * Author: Dong H. Ahn
 *
 * Update: 2014-01-29 DHA Import
 *
 */

#include <mpi.h>
#include <flux/core.h>
#include <math.h>
#include <values.h>
#include <unistd.h>

#include "kap.h"


typedef struct _VAC_t {
    double var;
    double accum;
    double count;
    double min;
    double max;
    double min_mean;
    double max_mean;
    double mean_accum;
    double mean_count;
    double min_std;
    double max_std;
    double std_accum;
} VAC_t;


/*********************************************************
 *                  STATIC DATA                          *
 *                                                       *
 *********************************************************/
static int _tester_rank = -1;
static int _tester_size = -1;
static flux_t *_hndl = NULL;


/*********************************************************
 *                  STATIC FUNCTIONS                     *
 *                                                       *
 *********************************************************/
static void
perf_report (VAC_t *results,
                double t, double b, 
                const char *l, int ao,
                FILE *fptr)
{
    /* CHANGE THIS TO VAC_t */
    fprintf (fptr,
        "Performance Summary (%s) \n", l);
    fprintf (fptr,
        "   Max Latency: %.2f usec\n",
        results->max);
    fprintf (fptr,
        "   Min Latency: %.2f usec\n",
        results->min);
    fprintf (fptr,
        "   Mean: %f usec\n",
        results->accum / results->count);
    fprintf (fptr,
        "   Std Deviation: %f \n",
        sqrt (results->var));
    fprintf (fptr,
        "   Throughput: %.2f OPs/s\n", t);
    fprintf (fptr,
        "   Total OP count: %f\n",
        results->count);
    if (ao) {
        fprintf (fptr,
        "   Bandwidth: %.2f Bytes/s\n", b);
    }
    fprintf (fptr,
        "+++++++++++++++++++++++++++++++++++++++\n");
    fprintf (fptr,
        "Sampling Distribution\n");
    fprintf (fptr,
        "   Max Mean: %f \n",
        results->max_mean);
    fprintf (fptr,
        "   Min Mean: %f \n",
        results->min_mean);
    fprintf (fptr,
        "   Mean Mean: %f \n",
        results->mean_accum/results->mean_count);
    fprintf (fptr,
        "   Max Std Dev.: %f \n",
        results->max_std);
    fprintf (fptr,
        "   Min Std Dev.: %f \n",
        results->min_std);
    fprintf (fptr,
        "   Mean Std Dev.: %f \n",
        results->std_accum/results->mean_count);

}


static void
reducer (void *in_a, void *inout_a, 
                int *len, MPI_Datatype *dptr)
{
    VAC_t *in;
    VAC_t *inout;
    double accum_X, accum_Y;
    double min_X, min_Y;
    double max_X, max_Y;
    double mean_X, mean_Y;
    double cnt_X, cnt_Y;
    double var_X, var_Y;
    double sq_cnt_X, sq_cnt_Y;
    double numer, denom;
    double t1, t2, t3, t4;

    double max_mean_X, max_mean_Y;
    double min_mean_X, min_mean_Y;
    double mean_accum_X, mean_accum_Y;
    double mean_cnt_X, mean_cnt_Y;
    double min_std_X, min_std_Y;
    double max_std_X, max_std_Y;
    double std_accum_X, std_accum_Y;

    if (*len != 1) 
        MPI_Abort (MPI_COMM_WORLD, 1);

    in = in_a;
    inout = inout_a;

    accum_X = in->accum;
    accum_Y = inout->accum; 

    if ( (accum_X != 0.0f) && (accum_Y != 0.0f) ) {
        min_X = in->min;
        max_X = in->max;
        min_Y = inout->min;
        max_Y = inout->max;
        max_mean_X = in->max_mean;
        max_mean_Y = inout->max_mean;
        min_mean_X = in->min_mean;
        min_mean_Y = inout->min_mean;
        mean_accum_X = in->mean_accum;
        mean_accum_Y = inout->mean_accum;
        mean_cnt_X = in->mean_count;
        mean_cnt_Y = inout->mean_count;
        std_accum_X = in->std_accum;
        std_accum_Y = inout->std_accum;
        min_std_X = in->min_std;
        min_std_Y = inout->min_std;
        max_std_X = in->max_std;
        max_std_Y = inout->max_std;


        cnt_X = in->count;
        cnt_Y = inout->count;
        sq_cnt_X = cnt_X*cnt_X;    
        sq_cnt_Y = cnt_Y*cnt_Y;    
        mean_X = accum_X / cnt_X;
        mean_Y = accum_Y / cnt_Y;
        var_X = in->var;
        var_Y = inout->var;

        t1 = sq_cnt_X*var_X + sq_cnt_Y*var_Y;
        t2 = cnt_Y*var_X + cnt_Y*var_Y 
             + cnt_X*var_X + cnt_X*var_Y;
        t3 = cnt_Y*cnt_X*var_X
             + cnt_Y*cnt_X*var_Y;
        t4 = cnt_X*cnt_Y
             *(mean_X - mean_Y)*(mean_X - mean_Y);

        numer = t1 - t2 + t3 + t4;
        denom = (cnt_X + cnt_Y - 1) * (cnt_X + cnt_Y);

        inout->var = numer/denom;
        inout->accum = accum_X + accum_Y;
        inout->count = cnt_X + cnt_Y; 
        inout->min = (min_X < min_Y)? min_X : min_Y;
        inout->max = (max_X > max_Y)? max_X : max_Y;
        inout->min_mean = (min_mean_X < min_mean_Y)
            ? min_mean_X : min_mean_Y;
        inout->max_mean = (max_mean_X > max_mean_Y)
            ? max_mean_X : max_mean_Y;
        inout->mean_accum = mean_accum_X + mean_accum_Y;
        inout->mean_count = mean_cnt_X + mean_cnt_Y;
        inout->min_std = (min_std_X < min_std_Y)
            ? min_std_X : min_std_Y;
        inout->max_std = (max_std_X > max_std_Y)
            ? max_std_X : max_std_Y;
        inout->std_accum = std_accum_X + std_accum_Y;

    }
    else {
        if (accum_X != 0.0f) {
            inout->var = in->var;
            inout->accum = in->accum;
            inout->count = in->count;
            inout->min = in->min;
            inout->max = in->max;
            inout->min_mean = in->min_mean;
            inout->max_mean = in->max_mean;
            inout->mean_accum = in->mean_accum;
            inout->mean_count = in->mean_count;
            inout->min_std = in->min_std;
            inout->max_std = in->max_std;
            inout->std_accum = in->std_accum; 
        }
    }
}


static void
reduce_results (perf_metric_t *s, VAC_t *a)
{
    VAC_t vac;
    MPI_Op var_op;
    MPI_Datatype vac_type;

    MPI_Type_contiguous (12, MPI_DOUBLE, &vac_type);    
    MPI_Type_commit (&vac_type);
    MPI_Op_create (reducer, 1, &var_op);
    
    vac.var = s->op_base.S;
    vac.accum = s->op_base.accum;
    vac.count = (double) s->op_base.op_count;
    vac.min = s->op_base.min;
    vac.max = s->op_base.max;
    vac.min_mean = s->op_base.accum/(double)s->op_base.op_count;
    vac.max_mean = s->op_base.accum/(double)s->op_base.op_count;
    vac.mean_accum = s->op_base.accum/(double)s->op_base.op_count;
    vac.mean_count = 1;
    vac.min_std = s->op_base.std;
    vac.max_std = s->op_base.std;
    vac.std_accum = s->op_base.std; 

    MPI_Allreduce (&vac, a, 1, vac_type, var_op, 
                   MPI_COMM_WORLD);

}


static int
metric_summary (kap_config_t *kc, 
                kap_personality_t *p, 
                perf_metric_t * m, 
                const char *l, 
                int ao, FILE *fptr)
{
    double t = 0.0f;
    double b = 0.0f;
    VAC_t results = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    reduce_results (m, &results);

    if (results.count == 0.0f) 
        return 0;

    t = results.count / (results.accum/1000000.0); 
    if (ao) {
        b = ((results.count) 
               * (double) kc->value_size)
              / (results.accum / 1000000.0);
    }
    if (p->rank == 0) 
        perf_report (&results, t, b, l, ao, fptr);

    return 0;
}


static int
phase_summary ( kap_personality_t *p,
                double b, double e, 
                const char *label,
                FILE *fptr)
{
    double elapse = e - b;
    double max_elapse;
    double min_elapse;

    MPI_Allreduce (&elapse, &max_elapse,
                   1, MPI_DOUBLE, MPI_MAX, 
                   MPI_COMM_WORLD); 
    if (elapse == 0.0f)
        elapse = DBL_MAX;
    MPI_Allreduce (&elapse, &min_elapse,
                   1, MPI_DOUBLE, MPI_MIN, 
                   MPI_COMM_WORLD); 
    if (p->rank == 0) {
        fprintf (fptr, 
            "   %s Max Time: %.2f usec \n", 
            label, max_elapse);
        fprintf (fptr, 
            "   %s Min Time: %.2f usec\n", 
            label, min_elapse);
    }

    return 0;
}


/*********************************************************
 *                  PUBLIC FUNCTIONS                     *
 *                                                       *
 *********************************************************/
void
kap_abort ()
{
    MPI_Abort (MPI_COMM_WORLD, 1);
}


int 
kap_commfab_init (int *argc, char ***argv)
{
    int ntries;
    if ( MPI_Init (argc, argv) != MPI_SUCCESS ) 
        return -1;
    if ( MPI_Comm_rank (MPI_COMM_WORLD, 
            &_tester_rank) != MPI_SUCCESS ) 
        return -2;
    if ( MPI_Comm_size (MPI_COMM_WORLD, 
            &_tester_size) != MPI_SUCCESS )
        return -3;

    if ( (_hndl = flux_open (NULL, 0)) == NULL ) {

        for (ntries = 0; ntries < 4; ++ntries) {
            sleep (5);
            if ( (_hndl = flux_open (NULL, 0)) != NULL ) {
                break;
            }
        }
        if (!_hndl) 
            return -4;
    } 

    return 0;
}


int 
kap_commfab_fini ()
{
    return MPI_Finalize ();
}


int
kap_commfab_size ()
{
    return _tester_size;
}


int
kap_commfab_rank ()
{
    return _tester_rank;
}


int 
personality (kap_config_t *kc, kap_personality_t *p)
{
    char bn[KAP_MAX_STR];

    if ( !p || !kc )
        return -1;

    p->rank = kap_commfab_rank ();
    p->size = kap_commfab_size ();
    p->iter_count = 0;
    p->role = k_none;
    p->handle = _hndl;

    kap_conf_postinit (kc, p->size);

    /*
     * Assume the rank distribution is cyclic
     * e.g., distributes rank processes to 
     * all of the nodes first before starting to
     * use the core on the first node again.
     */
    if ( p->rank < kc->nproducers ) {
        p->role = k_producer;
    } 
    if ( ((p->size-1) - p->rank) < kc->nconsumers ) {
        p->role = (p->role != k_producer) ?
                    k_consumer :
                    k_both; 
    }
    if (kc->sync_type == s_causal) {
        if ( IS_CONSUMER(p->role) ) {
            if (flux_event_subscribe (p->handle, 
                                KAP_CAUSAL_CONS_EV) < 0) {
                fprintf (stderr, 
                    "Failed: event sub.\n");
                goto error;
            }
        }
    }
    if (kc->iter_consumer > kc->iter_producer) {
        kc->iter_consumer = kc->iter_producer;
    
        fprintf (stderr, 
            "Warning: iter-consumer > than iter-producer.\n");
        fprintf (stderr, 
            "Warning: iter-consumer set to iter-producer.\n");
    }

    snprintf (bn, KAP_MAX_STR,
        "init-hb-%d",
        (int) kc->instance_num);
   
    /* to establish a clean happens-before relationship */
    if (flux_barrier (p->handle, 
            bn, kc->total_num_proc ) < 0) {
        fprintf (stderr,
            "Initial barrier failed.\n");
        goto error;
    } 

    return 0;

error:
    return -1;
}


int 
kap_commfab_perf_summary (kap_config_t *kc, 
                kap_personality_t *p)
{
    int rc = 0;

    FILE *putf = NULL;
    FILE *commitf = NULL;
    FILE *syncf = NULL;
    FILE *getf = NULL;
    FILE *wallf = NULL;

    if (p->rank == 0) {
        putf = fopen (PUTS_OUT_FN, "w");
        commitf = fopen (COMMITS_OUT_FN, "w");
        syncf = fopen (SYNC_OUT_FN, "w");
        getf = fopen (GETS_OUT_FN, "w");
        wallf = fopen (WALL_CLOCK_OUT_FN, "w");
        if (!putf || !commitf 
            || !syncf || !getf || !wallf ) {
            rc = -1;
            goto out;
        }
    }

    rc += metric_summary(kc, p, &Puts, "Puts", 
                         1, putf);
    rc += metric_summary(kc, p, &Commit_bn_Puts, 
                        "Commit_bn_Puts", 
                         0, commitf);
    rc += metric_summary(kc, p, &Sync_bn_Puts_Gets, 
                        "Sync_bn_Puts_Gets", 
                         0, syncf);
    rc += metric_summary(kc, p, &Gets, "Gets", 
                         1, getf);

    if (p->rank == 0) {
        fprintf (wallf, 
            "Wall Clock Time Summary \n"); 
    }

    rc += phase_summary (p, Begin_Prod_Phase, 
                         End_Prod_Phase,
                        "Producer Phase",
                         wallf);
    rc += phase_summary (p, Begin_Sync_Phase, 
                         End_Sync_Phase,
                        "Sync Phase",
                         wallf);
    rc += phase_summary (p, Begin_Cons_Phase, 
                         End_Cons_Phase,
                        "Consumer Phase",
                         wallf);
    rc += phase_summary (p, Begin_All, End_All,
                        "Total",
                         wallf);
out:
    if (putf)
        fclose (putf);
    if (commitf)
        fclose (commitf);
    if (syncf)
        fclose (syncf);
    if (getf)
        fclose (getf);
    if (wallf)
        fclose (wallf);
    return (rc < 0)? -1 : 0;
} 


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
