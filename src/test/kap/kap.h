/*
 * Author: Dong H. Ahn
 *
 * Update: 2014-01-29 DHA Import
 *
 */

#ifndef KAP_H
#define KAP_H 1

#include "kap_opts.h"
#include "kap_personality.h"



    /**
    @mainpage KVS Access Patterns Tester for Flux 
    @author Dong H. Ahn, Development Environment Group, Livermore Computing (LC) Division, LLNL

    To examine the performance of Flux's run-time system,
    we developed a tester called KVS Access Patterns (KAP).
    KAP models KVS access patterns through various interactions
    between KVS writers and readers. Writers are called producers;
    readers consumers.
    In essence, KAP allows a configurable number of producers
    to write key-value objects into our KVS
    and a configurable number of consumers to read these
    objects after ensuring the consistent KVS state.
    
    In addition to producer and/or consumer counts,
    KAP provides a range of parameters that can affect performance.
    Among them include the value size (of key-value objects),
    the number of objects to put,
    the number of objects to get, objects access
    patterns (through different striding), and
    synchronization primitives used for consistency.
    KAP consists of four phases: setup, producer, synchronization
    and consumer phases.
    During the setup phase, tester processes are launched
    into a set of nodes in which a CMB session had been established.
    They are assigned to ranks such that consecutive rank
    processes are distributed to consecutive nodes.
    Then, the rank processes determine their roles based
    on the command line arguments and issue a
    Flux collective barrier to begin to play their roles
    simultaneously.

    \image html figs/kaptester.jpg "Main phases of KAP" width=7cm
    \image latex figs/kaptester.pdf "Main phases of KAP" width=7cm
    
    Next, each producer calls the specified number of
    \c kvs_puts of an object of the specified value size.
    For each call, the producers use unique keys, but
    the values can be configured to be either unique
    or redundant.
    Once this is done, all of the producers and consumers
    enter the synchronization phase in which they
    participate in a consistency protocol. They use
    KVS synchronization primitives such as \c kvs_fence
    and \c kvs_wait_version.
    Finally, during the consumer phase, consumers read
    these key-value objects by calling \c kvs_gets.
    KAP provides options to emulate various read access
    patterns.

    The supported parameters are the following: 
    @verbatim
    Usage: KAP OPTIONS
    -h, --help                     Print this message.
    
    -l, --list-config              List the configuration.
    
    -T, --total-num-proc=PROC      Total num of procs (default=total num of cores).
    
    -P, --nproducers=PRODCOUNT     Num of producers (default=PROC).
    
    -P, --nconsumers=CONSCOUNT     Num of consumers (default=PROC).
    
    -p, --value-size=VALSIZE       Data size of a value produced by a put request
                                   of each producer (default=8B).
                                   VALSIZE must be a multiple of 8.
    
    -c, --cons-acc-count=ACCCOUNT  Num of key-value tuples each consumer gets.
    
    -a, --access-stride=STRIDE     Consumer's key access pattern. STRIDE=1 is
                                   a unit stride from your rank (i.e., the tuple 
                                   from (rank+1) mod P rank producer).
                                   STRIDE=2 is a stride of two, etc. When PROC/STRIDE
                                   is less than ACCCOUNT, access to unique keys
                                   is no longer be guarateed, as uniqueness depends on
                                   P modulo STRIDE (default=1).
    
    -i, --iter-producer=PR_ITER    Num of puts that each producer requests.
                                   Each iteration uses a unique directory and
                                   the keys and values are also unique (default=1).
    
    -f, --iter-commit=ITER         Num of producer iterations after which
                                   producers commit their states.
                                   ITER=0 means no commit (default=0).
    
    -t, --iter-commit-type=TYPE    A commit method between puts.
                                   TYPE: f=fence (default); s=serial commit.
    
    -j, --iter-consumer=CO_ITER    Num of iterations of each consumer to match 
                                   producers' iteration. Thus, the total num of 
                                   gets is a function of --cons-access-count and 
                                   this option (default=1). CO_ITER must be 
                                   smaller or equal to PR_ITER.
    
    -s, --sync-type=SYNCTYPE       Synchronization method bewteen producers
                                   and consumers. SYNCTYPE: f=collective fence
                                   (default); c=causal.
    
    -d, --ndirs=DIRCOUNT           Num of KVS directories across which tuples
                                   are distributed (default=1).
    
    -e, --redundant-val            Use redundant values instead of unique values
                                   KAP will use unique values without this option.
    
    @endverbatim

*/




typedef struct _kap_params_t {
    kap_personality_t pers;
    kap_config_t config;
} kap_params_t;


typedef struct _perf_base_t {
    double max;
    double min;
    double std;
    double accum;
    double M;
    double S;
    unsigned long op_count; 
} perf_base_t;


typedef struct _perf_metric_t {
    perf_base_t op_base;
    double throughput;
    double bandwidth; 
} perf_metric_t;


#define WALL_CLOCK_OUT_FN   "perf-wallclock.out"
#define WALL_CLOCK_DIST_FN  "perf-wallclock.dist"
#define PUTS_OUT_FN         "perf-puts.out"
#define PUTS_DIST_FN        "perf-puts.dist"
#define COMMITS_OUT_FN      "perf-commits.out"
#define COMMITS_DIST_FN     "perf-commits.dist"
#define SYNC_OUT_FN         "perf-sync.out"
#define SYNC_DIST_FN        "perf-sync.dist"
#define GETS_OUT_FN         "perf-gets.out"
#define GETS_DIST_FN        "perf-gets.dist"
#define KAP_MAX_STR         128
#define KAP_CAUSAL_CONS_EV  "causal" 
#define KAP_KVSVER_NAME     "version"
#define KAP_VAL_NAME        "V"
#define IS_PRODUCER(R) ((R == k_producer) || (R == k_both))
#define IS_CONSUMER(R) ((R == k_consumer) || (R == k_both))


extern perf_metric_t Puts;
extern perf_metric_t Commit_bn_Puts;
extern perf_metric_t Sync_bn_Puts_Gets;
extern perf_metric_t Gets;
extern double Begin_All;
extern double End_All;
extern double Begin_Prod_Phase;
extern double End_Prod_Phase;
extern double Begin_Sync_Phase;
extern double End_Sync_Phase;
extern double Begin_Cons_Phase;
extern double End_Cons_Phase;
extern double Begin;
extern double End;


extern double now ();
extern void update_metric (perf_metric_t *m, double b, double e);

//!
/*! 
    Run producer. Only producers should call this function to
    perform the producer's action.	 
    @param[in] param kap_params_t object that contains context.
    @return 0 on success, -1 on failure
*/
extern int run_producer (kap_params_t *param);

//!
/*! 
    Run consumer. Only consumers should call this function to
    perform the consumer's action.	 
    @param[in] param kap_params_t object that contains context.
    @return 0 on success, -1 on failure
*/
extern int run_consumer (kap_params_t *param);


//!
/*! 
    Run synchronization between producers and consumers. 
    @param[in] param kap_params_t object that contains context.
    @return 0 on success, -1 on failure
*/
extern int sync_prod_and_cons (kap_params_t *param);

#endif /* !KAP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

