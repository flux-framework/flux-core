/*
 * Commandline parser for KAP
 * Author: Dong H. Ahn
 *
 * Update: 2014-01-29 DHA Import
 *
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include "kap_opts.h"

#define CONFIG_OUT_FN       "config.out"

#define KAP_OPTS "hln:T:P:C:p:c:a:i:f:t:j:s:d:e"
static const struct option kap_opts[] = {
    {"help",            0, 0, 'h'},
    {"list-config",     0, 0, 'l'},
    {"instance-num",    1, 0, 'n'},
    {"total-num-proc",  1, 0, 'T'},
    {"nproducers",      1, 0, 'P'},
    {"nconsumers",      1, 0, 'C'},
    {"value-size",      1, 0, 'p'},
    {"cons-acc-count",  1, 0, 'c'},
    {"access-stride",   1, 0, 'a'},
    {"iter-producer",   1, 0, 'i'},
    {"iter-commit",     1, 0, 'f'},
    {"iter-commit-type",1, 0, 't'},
    {"iter-consumer",   1, 0, 'j'},
    {"sync-type",       1, 0, 's'},
    {"ndirs",           1, 0, 'd'},
    {"redundant-val",   0, 0, 'e'},
    { 0, 0, 0, 0 },
};


static void
print_usage ()
{
    fprintf (stderr,
    "Usage: KAP OPTIONS\n");

    fprintf (stderr,
    "-h, --help                     Print this message.\n\n");

    fprintf (stderr,
    "-l, --list-config              List the configuration.\n\n");

    fprintf (stderr,
    "-T, --total-num-proc=PROC      Total num of procs (default=total num of cores).\n\n");

    fprintf (stderr,
    "-P, --nproducers=PRODCOUNT     Num of producers (default=PROC).\n\n");

    fprintf (stderr,
    "-P, --nconsumers=CONSCOUNT     Num of consumers (default=PROC).\n\n");

    fprintf (stderr,
    "-p, --value-size=VALSIZE       Data size of a value produced by a put request\n"
    "                               of each producer (default=8B).\n"
    "                               VALSIZE must be a multiple of 8.\n\n"); 

    fprintf (stderr,
    "-c, --cons-acc-count=ACCCOUNT  Num of key-value tuples each consumer gets.\n\n");

    fprintf (stderr,
    "-a, --access-stride=STRIDE     Consumer's key access pattern. STRIDE=1 is\n");
    fprintf (stderr,
    "                               a unit stride from your rank (i.e., the tuple \n"
    "                               from (rank+1) mod P rank producer).\n" 
    "                               STRIDE=2 is a stride of two, etc. When PROC/STRIDE\n" 
    "                               is less than ACCCOUNT, access to unique keys\n" 
    "                               is no longer be guarateed, as uniqueness depends on\n" 
    "                               P modulo STRIDE (default=1).\n\n"); 

    fprintf (stderr,
    "-i, --iter-producer=PR_ITER    Num of puts that each producer requests.\n" 
    "                               Each iteration uses a unique directory and\n"
    "                               the keys and values are also unique (default=1).\n\n");

    fprintf (stderr,
    "-f, --iter-commit=ITER         Num of producer iterations after which\n" 
    "                               producers commit their states.\n"
    "                               ITER=0 means no commit (default=0).\n\n"); 

    fprintf (stderr,
    "-t, --iter-commit-type=TYPE    A commit method between puts.\n"  
    "                               TYPE: f=fence (default); s=serial commit.\n\n");

    fprintf (stderr,
    "-j, --iter-consumer=CO_ITER    Num of iterations of each consumer to match \n"
    "                               producers' iteration. Thus, the total num of \n"
    "                               gets is a function of --cons-access-count and \n"
    "                               this option (default=1). CO_ITER must be \n"
    "                               smaller or equal to PR_ITER.\n\n"); 

    fprintf (stderr,
    "-s, --sync-type=SYNCTYPE       Synchronization method bewteen producers\n"
    "                               and consumers. SYNCTYPE: f=collective fence\n"
    "                               (default); c=causal.\n\n");

    fprintf (stderr,
    "-d, --ndirs=DIRCOUNT           Num of KVS directories across which tuples\n"
    "                               are distributed (default=1).\n\n");

    fprintf (stderr,
    "-e, --redundant-val            Use redundant values instead of unique values\n"
    "                               KAP will use unique values without this option.\n\n");

    exit (1);
}


void
kap_conf_init (kap_config_t *kap_conf)
{
    kap_conf->total_num_proc = 0xffffffffffffffff;
    kap_conf->instance_num = 0;
    kap_conf->nproducers = 0xffffffffffffffff;
    kap_conf->nconsumers = 0xffffffffffffffff;
    kap_conf->value_size = 8;
    kap_conf->consumer_access_count = 1;
    kap_conf->access_stride = 1;
    kap_conf->iter_producer = 1;
    kap_conf->iter_commit= 0;
    kap_conf->iter_commit_type = s_fence;
    kap_conf->iter_consumer = 1;
    kap_conf->sync_type = s_fence;
    kap_conf->ndirs = 1;
    kap_conf->redundant_val = 0;
    kap_conf->list_config = 0;
}


int
kap_conf_postinit (kap_config_t *kap_conf, long tcc)
{
    int rc = 0; 

    if (kap_conf->total_num_proc == 0xffffffffffffffff) {
        rc = 1;
        kap_conf->total_num_proc = tcc;
    }
    if (kap_conf->nproducers == 0xffffffffffffffff) {
        rc = 1;
        kap_conf->nproducers = tcc;
    }
    if (kap_conf->nconsumers == 0xffffffffffffffff) {
        rc = 1;
        kap_conf->nconsumers = tcc;
    }

    return rc;
}


void 
print_config (kap_config_t *kap_conf)
{
    FILE *fptr = NULL;

    if ( !(fptr = fopen (CONFIG_OUT_FN, "w")) ) 
        return;
    
    fprintf (fptr,
        "Configuration Summary\n");
    fprintf (fptr, 
        "   total_num_proc: %lu\n", kap_conf->total_num_proc);
    fprintf (fptr, 
        "   nproducers: %lu\n", kap_conf->nproducers);
    fprintf (fptr, 
        "   nconsumers: %lu\n", kap_conf->nconsumers);
    fprintf (fptr, 
        "   value_size: %lu bytes\n", kap_conf->value_size);
    fprintf (fptr, 
        "   consumer_access_count: %lu\n", 
        kap_conf->consumer_access_count);
    fprintf (fptr, 
        "   access_stride: %u\n", kap_conf->access_stride);
    fprintf (fptr, 
        "   iter_producer: %u\n", kap_conf->iter_producer);
    fprintf (fptr, 
        "   iter_commit: %u\n", kap_conf->iter_commit);
    fprintf (fptr, 
        "   iter_commit_type: %u\n", kap_conf->iter_commit_type);
    fprintf (fptr, 
        "   iter_consumer: %u\n", kap_conf->iter_consumer);
    fprintf (fptr, 
        "   sync_type: %s\n", 
        (kap_conf->sync_type == s_fence)? "fence" : "causal");
    fprintf (fptr, 
        "   ndirs: %u\n", kap_conf->ndirs);
    fprintf (fptr, 
        "   instance_num: %lu\n", kap_conf->instance_num);
    fprintf (fptr, 
        "   redundant_val: %u\n", kap_conf->redundant_val);
}


int 
parse_kap_opts (int argc, char *argv[], kap_config_t *kap_conf)
{
    int c;

    kap_conf_init (kap_conf);    

    while ( (c = getopt_long (argc, argv, 
                 KAP_OPTS, kap_opts, NULL)) != -1) {
        switch (c) {
            case 'h':
                print_usage ();
                break;
            case 'l':
                kap_conf->list_config = 1;
                break;
            case 'n':
                kap_conf->instance_num 
                    = strtoul (optarg, NULL, 10);
                break;
            case 'T':
                kap_conf->total_num_proc 
                    = strtoul (optarg, NULL, 10);
                break;
            case 'P':
                kap_conf->nproducers
                    = strtoul (optarg, NULL, 10);
                break;
            case 'C':
                kap_conf->nconsumers 
                    = strtoul (optarg, NULL, 10);
                break;
            case 'p':
                kap_conf->value_size
                    = strtoul (optarg, NULL, 10);
                break;
            case 'c':
                kap_conf->consumer_access_count
                    = strtoul (optarg, NULL, 10);
                break;
            case 'a':
                kap_conf->access_stride
                    = (unsigned int) strtoul (optarg, NULL, 10);
                break;
            case 'i':
                kap_conf->iter_producer
                    = (unsigned int) strtoul (optarg, NULL, 10);
                break;
            case 'f':
                kap_conf->iter_commit
                    = (unsigned int) strtoul (optarg, NULL, 10);
                break;
            case 't':
                if ( strcmp(optarg, "f") == 0) {
                    kap_conf->iter_commit_type = s_fence;
                }
                else if ( strcmp(optarg, "s") == 0) {
                    kap_conf->iter_commit_type = s_commit;
                }
                else {
                    print_usage ();
                }
                break;
            case 'j':
                kap_conf->iter_consumer
                    = (unsigned int) strtoul (optarg, NULL, 10);
                break;
            case 's':
                if ( strcmp(optarg, "f") == 0) {
                    kap_conf->sync_type = s_fence;
                }
                else if ( strcmp(optarg, "c") == 0) {
                    kap_conf->sync_type = s_causal;
                }
                else {
                    print_usage ();
                }
                break;
            case 'd':
                kap_conf->ndirs
                    = (unsigned int) strtoul (optarg, NULL, 10);
                break;
            case 'e':
                kap_conf->redundant_val = 1;
                break;
            default:
                print_usage ();
                break;
        }
    }

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

