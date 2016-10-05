/*
 * Author: Dong H. Ahn
 *
 * Update: 2014-01-29 DHA Import
 *
 */

#ifndef KAP_PERSONALITY_H
#define KAP_PERSONALITY_H 1

#include <flux/core.h>
#include "kap_opts.h"

typedef enum _kap_role_e {
    k_none = 0,
    k_producer,
    k_consumer,
    k_both 
} kap_role_e;

typedef struct _kap_personality_t {
    int rank;
    int size;
    int iter_count;
    kap_role_e role;
    flux_t *handle; 
} kap_personality_t;

//!
/*!
    Initialize the underlying comm. fab such as MPI
    @param[in] argc argc from main 
    @param[in] argv argv from main 
    @return 0 on success, -1 on failure 
*/
extern int kap_commfab_init (int *argc, char ***argv);

//!
/*!
    Abort the the underlying comm. for MPI, this is MPI_Abort
*/
extern void kap_abort ();

//!
/*!
    Finalize the underlying comm. fab such as MPI
    @return 0 on success, -1 on failure 
*/
extern int kap_commfab_fini ( );

//!
/*!
    Return the global size
    @return the number of processes 
*/
extern int kap_commfab_size ();

//!
/*!
    Return the global rank 
    @return rank 
*/
extern int kap_commfab_rank ();

//!
/*!
    Determine personality (e.g., if I'm a producer or consumer
    or both.
    @param[in] kc config obj 
    @param[in] p personality obj 
    @return 0 on success, -1 on failure 
*/
extern int kap_commfab_perf_summary (kap_config_t *kc, kap_personality_t *p);

//!
/*!
    Determine personality (e.g., if I'm a producer or consumer
    or both.
    @param[in] kc configuration obj that has been filled in
    @param[out] p personality
    @return 0 on success, -1 on failure 
*/
extern int personality (kap_config_t *kc, kap_personality_t *p);

#endif /* KAP_PERSONALITY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
