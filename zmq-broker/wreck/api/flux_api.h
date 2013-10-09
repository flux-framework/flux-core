/*
 * $Header: $
 *--------------------------------------------------------------------------------
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC. Produced at
 * the Lawrence Livermore National Laboratory. Written by Dong H. Ahn <ahn1@llnl.gov>.
 *--------------------------------------------------------------------------------
 *
 *  Update Log:
 *        Oct 07 2013 DHA: Copied from the mockup API and modified
 *        Aug 22 2013 DHA: Created file.
 */

#ifndef FLUX_API_H_
#define FLUX_API_H_

/**
    @mainpage FLUX Application Programming Interface (API) 
    @author Dong H. Ahn, Development Environment Group, 
    Livermore Computing Division, LLNL

     
    @section intro Description
    FLUX API defines user-space programming interface that allow run-time tools 
    under FLUX resource management software system to launch, control, and destroy 
    various software program processes effectively and scalably. The design point
    of FLUX API is to faciliate easy integratoin of essential high-end computing 
    elements such as scalable debuggers, performance analyzers, specialized 
    middelware subsystems,into FLUX. This API treats MPI programs and
    other tun-time or middleware programs alike as first-class citizens.
    Thus, in addition to conventional services like launching, this API provides 
    effecient monitoring, notification, synchronization and control 
    mechanisms. In particular, FLUX API
    has been co-designed with a significant modification within
    LaunchMON, scalable infrastructure for tool deamon launching. 
    The results show that run-time tools can tranparently be
    run alongside one another, building on the strength
    of one another. 
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <flux_lwj_desc.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


//!
/*! 
    Interface that initializes this FLUX API
*/
extern flux_rc_e FLUX_init ();
	         

//!
/*! 
    Interface that finalizes this FLUX API
*/
extern flux_rc_e FLUX_fini ();


//!
/*! 
    Interface that creates a lightweight job (LWJ) job context 

    @param[out] lwj new lwj context
*/
extern flux_rc_e FLUX_update_createLWJCxt (
	         flux_lwj_id_t *lwj);


//!
/*! 
    Interface that creates a lightweight job (LWJ) job context 

    @param[in] lwj target lwj context to destroy
*/
extern flux_rc_e FLUX_update_destoryLWJCxt (
	         const flux_lwj_id_t *lwj);


//!
/*! 
    Interface that converts the pid of the RM starter process 
    to its LWJ id. If the tool wants to work on an LWJ that is 
    already running, this will come in handy. 

    @param[in] starter information on the starter process
    @param[out] lwj corresponding lwj context 
*/
extern flux_rc_e FLUX_query_pid2LWJId (
                 const flux_starter_info_t *starter,
		 flux_lwj_id_t *lwj);


//!
/*! 
    Interface that converts the target lwj to the LWJ 
    information including RM starter process info.  

    @param[in] lwj target lwj 
    @param[out] lwj_info info for lwj
*/
extern flux_rc_e FLUX_query_LWJId2JobInfo (
	         const flux_lwj_id_t *lwj, 
		 flux_lwj_info_t *lwj_info);


//!
/*! 
    Interface that returns the size of the global MPIR 
    process table 

    @param[in] lwj target lwj 
    @param[out] count the number of processes in lwj
*/
extern flux_rc_e FLUX_query_globalProcTableSize (
	         const flux_lwj_id_t *lwj,
		 size_t *count);


//!
/*! 
    Interface that returns the global MPIR 
    process table. If ptab_buf is smaller than the actual process 
    table size, ptab_buf will be truncated and this condition is 
    when ptab_buf_size is less than ret_ptab_size. 

    @param[in] lwj target lwj 
    @param[out] ptab_buf buffer to hold the global process table
    @param[in] ptab_buf_size the size of ptab_buf
    @param[out] ret_ptab_size the size of the returned process table 
*/
extern flux_rc_e FLUX_query_globalProcTable (
	         const flux_lwj_id_t *lwj,
		 MPIR_PROCDESC_EXT *ptab_buf, 
		 const size_t ptab_buf_size,
                 size_t *ret_ptab_size);

//!
/*! 
    Interface that returns the size of the local MPIR process 
    table associated with lwj into count based on the hostname (hn) 
    or where this call is made if hn is NULL. 

    @param[in] lwj target lwj 
    @param[in] hostname the node from which to get the local lwj process info 
    @param[out] count the size of the local process table
*/
extern flux_rc_e FLUX_query_localProcTableSize (
	         const flux_lwj_id_t *lwj, 
		 const char *hostname, 
		 size_t *count);


//!
/*! 
    Interface that returns the local MPIR 
    process table 

    @param[in] lwj target lwj 
    @param[in] hostname the node from which to get the local lwj process info 
    @param[out] ptab_buf buffer to hold the local process table
    @param[in] ptab_buf_size size of ptab_buf
    @param[out] ret_ptab_size size of the returned local process table
*/
extern flux_rc_e FLUX_query_localProcTable (
	         const flux_lwj_id_t *lwj,
		 const char *hostname,
		 MPIR_PROCDESC_EXT *ptab_buf, 
		 const size_t ptab_buf_size,
                 size_t *ret_ptab_size);

//!
/*! 
    Interface that allows the caller to fetch the status of the lwj.
    This can be used with a periodic polling scheme to monitor status 
    update.

    @param[in] lwj target lwj 
    @param[in] status the current state of lwj 
*/
extern flux_rc_e FLUX_query_LWJStatus (
	         const flux_lwj_id_t *lwj, 
		 flux_lwj_status_e *status);


//!
/*! 
    Interface that allows the caller to register a status 
    callback function, which is invoked whenever the status of lwj 
    is changed--e.g., just created and stopped for sync; lwj terminiated 
    (we will need to design the state changes of an lwj and notify many
    state changes to the callback with some mask support). 
    If we don't want the callback style as this might require 
    the API implementation to be muli-process or multi-threaded, 
    we can alternatively design a polling mechanism bove.

    @param[in] lwj target lwj 
    @param[in] cback call-back to be invoked on each state change
*/
extern flux_rc_e FLUX_monitor_registerStatusCb (
	         const flux_lwj_id_t *lwj, 
		 int (*cback) (flux_lwj_status_e *status));



//!
/*! 
    Interface that launches target application or tool daemons given 
    an executable and a list of arguments. If the sync flag is true, 
    this interface spawns the processes and leave them stopped.
    If the coloc_lwj is not NULL, this will co-locate the specified 
    executable (e.g., tool daemon path) with the processes of the coloc_lwj 
    LWJ. Note that we need to determine the rest of the options/arguments 
    so that we can overload this interface for launching 
    of both application processes and tool/middleware daemons. 
    For the co-location, we need to express subset vs. full-set launching etc. 

    @param[in] lwj to be launched
    @param[in] sync if 1, lwj must be stopped on spawn to be sync'ed with another lwj 
    @param[in] coloc_lwj target lwj that lwj me must be co-located with  
    @param[in] lwjpath executable path to the lwj program
    @param[in] lwjargv commandline vector for the lwj program, the first element must be the lwj program path  
    @param[in] coloc if 1, lwj is co-located with coloc_lwj 
    @param[in] nnodes number of nodes to run on 
    @param[in] nproc_per_node number of processes per ndoe run with  
*/
extern flux_rc_e
FLUX_launch_spawn (
		 const flux_lwj_id_t *lwj, 
		 int sync, 
		 const flux_lwj_id_t *coloc_lwj,
                 const char * lwjpath,
		 char * const lwjargv[],
                 int coloc,
		 int nnodes,
		 int nproc_per_node);


//!
/*! 
    Interface that kills and cleans up all of the processes associated 
    with the target LWJ 

    @param[in] lwj target lwj to kill 
*/
extern flux_rc_e FLUX_control_killLWJ (
                 const flux_lwj_id_t *lwj); 


//!
/*! 
    Interface that cotinues LWJ processes  

    @param[in] lwj target lwj to continue
*/
extern flux_rc_e FLUX_control_continueLWJs (
                 const flux_lwj_id_t *lwj); 


//!
/*! 
    Sets a FILE pointer to channel the output

    @param[in] newfd open FILE stream pointer
*/
extern FILE * set_log_fd (FILE *newfd);


//!
/*! 
    Sets a file  

    @param[in] level verbosity level: 0 for error, higher for info level
    
*/
extern unsigned int set_verbose_level (unsigned int level);


//!
/*! 
    Utility funtion for logging. By default, this
    pushes the output to stdout at the verbosity level=0.

    @param[in] format a print string with printf format
    @param[in] error 0 for error, 1 or higher for info 
*/
extern void error_log (const char *format, unsigned int error, ...);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*  FLUX_API_H_ */
