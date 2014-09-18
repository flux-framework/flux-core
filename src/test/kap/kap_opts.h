/*
 * Author: Dong H. Ahn
 *
 * Update: 2014-01-29 DHA Import
 *
 */

#ifndef KAP_OPTS_H
#define KAP_OPTS_H 1

typedef enum _sync_type_e {
    s_fence = 0,
    s_commit,
    s_causal
} sync_type_e;

typedef struct _kap_config_t {
    unsigned long total_num_proc;
    unsigned long instance_num;
    unsigned long nproducers;
    unsigned long nconsumers;
    unsigned long value_size;
    unsigned long consumer_access_count;
    unsigned int access_stride;
    unsigned int iter_producer;
    unsigned int iter_commit;
    unsigned int iter_commit_type;
    unsigned int iter_consumer;
    sync_type_e sync_type;
    unsigned int ndirs;
    unsigned int redundant_val;
    unsigned int list_config;
} kap_config_t; 

//!
/*! 
    Initialize the rest of parameters that depends on comm
    fabric initialization.
    @param[out] kap_conf configuration object to be init
    @param[in] tcc total core count in the allocation
    @return 1 when kap_conf has changed otherwise 0
*/
extern int kap_conf_postinit (kap_config_t *kap_conf, long tcc);


//!
/*! 
    Print configuration.
    @param[out] kap_conf configuration object to list 
*/
extern void print_config (kap_config_t *kap_conf);


//!
/*! 
    parse command line and fill the test parameters.
    @param[in] argc argc from main 
    @param[in] argv argv from main 
    @param[out] kap_conf configuration object to be filled 
    @return 0 on success, -1 on failure
*/
extern int parse_kap_opts (int argc, char *argv[], kap_config_t *kap_conf);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

#endif /* !KAP_OPTS_H */
