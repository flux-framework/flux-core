/*
 * $Header: $
 *--------------------------------------------------------------------------------
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC. Produced at
 * the Lawrence Livermore National Laboratory. Written by Dong H. Ahn <ahn1@llnl.gov>.
 *--------------------------------------------------------------------------------
 *
 *  Update Log:
 *        Oct 07 2013 DHA: Created 
 */


#ifndef FLUX_LWJ_DESC_H_
#define FLUX_LWJ_DESC_H_


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


//!
/*! 
    Enumerates return codes from Flux API calls
*/
typedef enum _flux_rc_e {
    FLUX_OK,
    FLUX_ERROR,
    FLUX_NOT_IMPL
} flux_rc_e;


//!
/*! 
    Enumerates the states of an LWJ
*/
typedef enum _flux_lwj_status_e {
    status_null = 0,        /*!< created but not registered */
    status_registered,      /*!< registered */
    status_spawn_requested, /*!< registered */
    status_spawned_stopped, /*!< the target spawned and stopped */
    status_spawned_running, /*!< the target spawned and running */
    status_running,         /*!< the target running */
    status_attach_requested,/*!< attach requested */
    status_kill_requested,  /*!< kill requested */
    status_aborted,         /*!< the target aborted */
    status_completed,       /*!< the target completed */
    status_unregistered,    /*!< unregistered */
    status_reserved
} flux_lwj_status_e;


//!
/*! 
    Extended proctable type:
    Clients must define X_MPIR_PROCDESC_DEFINED if
    they already define MPIR_PROCDESC* types
*/
#ifndef X_MPIR_PROCDESC_DEFINED  
typedef struct {
    char *host_name;       /*!< Something we can pass to inet_addr */
    char *executable_name; /*!< The name of the image */
    int pid;               /*!< The pid of the process */
} MPIR_PROCDESC;

typedef struct {
    MPIR_PROCDESC pd;
    int mpirank;
    int cnodeid;
} MPIR_PROCDESC_EXT;
#endif 


//!
/*! 
    flux_lwj_id_t is unsigned int for now
*/
typedef int64_t flux_lwj_id_t;


//!
/*! 
    Info block for LWJ starter 
*/
typedef struct _flux_starter_info_t {
    char *hostname;  /*!< a node where the starter is running */
    pid_t pid;       /*!< pid of the starter process */
} flux_starter_info_t;


//!
/*! 
    Info block for LWJ. This should be augmeneted as 
    we continue to evole both FLUX run-time and FluxAPI. 
*/
typedef struct _flux_lwj_info_t {
    flux_lwj_id_t lwj;       /*!< lwj context */
    flux_lwj_status_e status;  /*!< lwj's state */
    flux_starter_info_t starter; 
    int proc_table_size;    /*!< size of proc_table */
} flux_lwj_info_t;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FLUX_LWJ_DESC_H_ */
