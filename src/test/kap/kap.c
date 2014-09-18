/*
 * KVS Access Patterns (KAP) Tester's main driver
 * Author: Dong H. Ahn
 *
 * Update: 2014-01-29 DHA Import
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>

#include "kap.h"


/*********************************************************
 *                  GLOBAL DATA                          *
 *                                                       *
 *********************************************************/
perf_metric_t Puts = { 
    .op_base    = { .max = 0.0f, 
                    .min = DBL_MAX, 
                    .std = 0.0f, 
                    .accum = 0.0f, 
                    .op_count = 0}, 
    .throughput = 0.0f, 
    .bandwidth  = 0.0f};

perf_metric_t Commit_bn_Puts = {
    .op_base    = { .max = 0.0f, 
                    .min = DBL_MAX, 
                    .std = 0.0f, 
                    .accum = 0.0f, 
                    .op_count = 0}, 
    .throughput = 0.0f, 
    .bandwidth  = 0.0f};

perf_metric_t Sync_bn_Puts_Gets = {
    .op_base    = { .max = 0.0f, 
                    .min = DBL_MAX, 
                    .std = 0.0f, 
                    .accum = 0.0f, 
                    .op_count = 0}, 
    .throughput = 0.0f, 
    .bandwidth  = 0.0f};

perf_metric_t Gets = {
    .op_base    = { .max = 0.0f, 
                    .min = DBL_MAX, 
                    .std = 0.0f, 
                    .accum = 0.0f, 
                    .op_count = 0}, 
    .throughput = 0.0f, 
    .bandwidth  = 0.0f};

double Begin_All = 0.0f;
double End_All = 0.0f;
double Begin_Prod_Phase = 0.0f;
double End_Prod_Phase = 0.0f;
double Begin_Sync_Phase = 0.0f;
double End_Sync_Phase = 0.0f;
double Begin_Cons_Phase = 0.0f;
double End_Cons_Phase = 0.0f;
double Begin = 0.0f;
double End = 0.0f;


/*********************************************************
 *                  STATIC FUNCTIONS                     *
 *                                                       *
 *********************************************************/
static int 
kap_init (int *argc, char ***argv, 
                kap_personality_t *p, kap_config_t *kc)
{
    if ( kap_commfab_init (argc, argv) < 0 ) {
        fprintf (stderr, 
            "kap_tester_init failed.\n");
        return -1;
    } 

    return 0;
}


static void
fatal ()
{
    kap_abort ();
    kap_commfab_fini (); 
    exit (1);
}


static void 
summarize (kap_params_t *params, perf_metric_t *m, int ao)
{
    m->throughput = ((double) m->op_base.op_count)
                    / (m->op_base.accum / 1000000.0);
    if (ao) {
        m->bandwidth = ((double)(m->op_base.op_count) 
                         * (double) params->config.value_size)
                    / (m->op_base.accum / 1000000.0);
    }
}


/*********************************************************
 *                  PUBLIC FUNCTIONS                     *
 *                                                       *
 *********************************************************/
double 
now ()
{
    struct timeval ts;
    double rt;
    gettimeofday (&ts, NULL);
    rt = ((double) (ts.tv_sec)) * 1000000.0;
    rt += (double) ts.tv_usec;
    return rt;
}


void 
update_metric (perf_metric_t *m, double b, double e)
{
    double elapse = e - b;
    if (m->op_base.op_count == 0) {
        m->op_base.op_count++;
        m->op_base.M = elapse;
        m->op_base.S = elapse;
        m->op_base.std = 0.0f;
    }
    else {
        /*
         * Running std algorithm using Welford's method.
         */ 
        double old_M; 
        double old_S;
        m->op_base.op_count++;
        old_M = m->op_base.M;
        old_S = m->op_base.S;
        m->op_base.M = old_M 
            + ((elapse - old_M)/m->op_base.op_count);
        m->op_base.S = old_S 
            + ((elapse - old_M)*(elapse - m->op_base.M));
        m->op_base.std = sqrt ((m->op_base.S) 
            / (m->op_base.op_count - 1));
    }

    m->op_base.accum += elapse;
    
    if (elapse > m->op_base.max) 
        m->op_base.max = elapse; 
    if (elapse < m->op_base.min) 
        m->op_base.min = elapse; 
}


int 
main (int argc, char *argv[])
{
    kap_params_t param;

    if ( parse_kap_opts (argc, argv, &(param.config)) < 0 ) {
        fprintf (stderr, "Failed to parse optionsp.\n");
        goto error;
    }
    if ( kap_init (&argc, &argv, 
                    &(param.pers), &(param.config)) < 0 ) {
        fprintf (stderr, "Failed to init KAP.\n");
        goto error;
    }
    if ( personality (&(param.config), &(param.pers)) < 0 ) {
        fprintf (stderr, "Failed to set personalities.\n");
        goto error;
    }
    if (param.config.list_config == 1) {
        if (param.pers.rank == 0) {
            print_config (&(param.config));
        }
    }

    /**********************************************************
     *          BEGIN KVS ACCESS PATTERN TEST                 *
     *                                                        *
     **********************************************************/
    if (param.pers.role != k_none)
        Begin_All = now ();

    if ( IS_PRODUCER(param.pers.role) ) {
        Begin_Prod_Phase = now ();
        if ( (run_producer (&param) < 0) ) {
            fprintf (stderr, "Failed to run producers.\n");
            goto error;
        }
        End_Prod_Phase = now ();
    }
    if (param.pers.role != k_none) {
        Begin_Sync_Phase = now ();
        if ( sync_prod_and_cons (&param) < 0 ) {
            fprintf (stderr, 
                "Failed to synchronize between producers "
                "and consumers.\n");
            goto error;
        }
        End_Sync_Phase = now ();
    }
    if ( IS_CONSUMER(param.pers.role) ) {
        Begin_Cons_Phase = now ();
        if ( (run_consumer (&param) < 0) ) { 
            fprintf (stderr, 
                "Failed to run consumers\n");
            goto error;
        }
        End_Cons_Phase = now ();
    }

    if (param.pers.role != k_none)
        End_All = now ();
    /**********************************************************
     *          END KVS ACCESS PATTERN TEST                   *
     *                                                        *
     **********************************************************/

    summarize (&param, &Puts, 1);
    summarize (&param, &Commit_bn_Puts, 0);
    summarize (&param, &Sync_bn_Puts_Gets, 0);
    summarize (&param, &Gets, 1);
    
    kap_commfab_perf_summary (&(param.config),
                              &(param.pers)); 

    return EXIT_SUCCESS;
error:
    fatal ();
    return EXIT_FAILURE;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

