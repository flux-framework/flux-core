/*
 * $Header: $
 *--------------------------------------------------------------------------------
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC. Produced at
 * the Lawrence Livermore National Laboratory. Written by Dong H. Ahn <ahn1@llnl.gov>.
 * LLNL-CODE-409469. All rights reserved.
 *
 * This file is part of LaunchMON. For details, see
 * https://computing.llnl.gov/?set=resources&page=os_projects
 *
 * Please also read LICENSE.txt -- Our Notice and GNU Lesser General Public License.
 *
 *
 * This program is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License (as published by the Free Software
 * Foundation) version 2.1 dated February 1999.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 *--------------------------------------------------------------------------------
 *
 *  Update Log:
 *        Aug 22 2013 DHA: Created file.
 */

#ifndef FLUX_API_MOCKUP_H_
#define FLUX_API_MOCKUP_H_

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef enum _flux_rc_e {
    FLUX_OK,
    FLUX_ERROR
} flux_rc_e;

typedef enum _flux_lwj_event_e {
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
} flux_lwj_event_e;

#ifndef LMON_API_LMON_PROC_TAB_H
typedef struct {
    char *host_name;       /* Something we can pass to inet_addr */
    char *executable_name; /* The name of the image */
    int pid;               /* The pid of the process */
} MPIR_PROCDESC;

typedef struct {
    MPIR_PROCDESC pd;
    int mpirank;
    int cnodeid;
} MPIR_PROCDESC_EXT;
#endif

typedef struct _flux_lwj_id {
    int id;
} flux_lwj_id_t;

typedef struct _flux_lwj_info_t
{
    flux_lwj_id_t lwj;
    int status;
    char *hn;
    pid_t pid;
    int procTableSize;
    MPIR_PROCDESC_EXT *procTable;
} flux_lwj_info_t;


#define FLUX_MOCKUP_PID      12345
#define FLUX_MOCKUP_LWJ_ID   100
#define FLUX_MOCKUP_HOSTNAME "sierra324"
#define FLUX_MOCKUP_EXEC     "/foo/bar"
#define FLUX_MOCKUP_STATUS   5

//!
/*! 
    Interface that initializes this FLUX API
*/
extern flux_rc_e FLUX_init ();
	         

//!
/*! 
    Interface that creates a lightweight job (LWJ) job context 
*/
extern flux_rc_e FLUX_update_createLWJCxt (
	         flux_lwj_id_t *lwj);


//!
/*! 
    Interface that creates a lightweight job (LWJ) job context 
*/
extern flux_rc_e FLUX_update_destoryLWJCxt (
	         flux_lwj_id_t *lwj);


//!
/*! 
    Interface that converts the pid of the RM starter process 
    to its LWJ id. If the tool wants to work on an LWJ that is 
    already running, this will come in handy. 
*/
extern flux_rc_e FLUX_query_pid2LWJId (
	         const char *hn, 
		 pid_t pid, 
		 flux_lwj_id_t *lwj);


//!
/*! 
    Interface that converts the target LWJ id to the LWJ 
    information that includes the information on the RM starter 
    process.  flux_lwj_info_t needs to be defined 
*/
extern flux_rc_e FLUX_query_LWJId2JobInfo (
	         const flux_lwj_id_t *lwj, 
		 flux_lwj_info_t *info);


//!
/*! 
    Interface that returns the size of the global MPIR 
    process table 
*/
extern flux_rc_e FLUX_query_globalProcTableSize (
	         const flux_lwj_id_t *lwj,
		 size_t *count);


//!
/*! 
    Interface that returns the global MPIR 
    process table 
*/
extern flux_rc_e FLUX_query_globalProcTable (
	         const flux_lwj_id_t *lwj,
		 MPIR_PROCDESC_EXT *pt, 
		 size_t count);

//!
/*! 
    Interface that returns the size of the local MPIR process 
    table associated with lwj into count based on the hostname (hn) 
    or where this call is made if hn is NULL. 
*/
extern flux_rc_e FLUX_query_localProcTableSize (
	         flux_lwj_id_t *lwj, 
		 const char *hn, 
		 size_t *count);


//!
/*! 
    Interface that returns the global MPIR 
    process table 
*/
extern flux_rc_e FLUX_query_localProcTable (
	         const flux_lwj_id_t *lwj,
		 const char *hn,
		 MPIR_PROCDESC_EXT *pt, 
		 size_t count);

//!
/*! 
    Interface that allows the caller to fetch the status of the lwj.
    This can be used with a periodic polling scheme to monitor status 
    update.
*/
extern flux_rc_e FLUX_query_LWJStatus (
	         flux_lwj_id_t *lwj, 
		 int *status);


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
*/
extern flux_rc_e FLUX_monitor_registerStatusCb (
	         const flux_lwj_id_t *lwj, 
		 int (*cb) (int *status));



//!
/*! 
    Interface that launches target application or tool daemons given 
    an executable and a list of arguments. If the sync flag is true, 
    this interface should spawn the processes and leave them stopped.
    If the target LWJ is not NULL, this will co-locate the specified 
    executable (e.g., tool daemon path) with the processes of the target 
    LWJ. Note that we need to determine the rest of the options/arguments 
    so that we can overload this interface for launching 
    of both application processes and tool/middleware daemons. 
    For the co-location, we need to express subset vs. full-set launching etc. 
*/
extern flux_rc_e
FLUX_launch_spawn (
		 const flux_lwj_id_t *me, 
		 int sync, 
		 const flux_lwj_id_t *target,
                 const char *lwjpath,
		 char * const lwjargv[],
                 int coloc,
		 int nn,
		 int np /*...*/);


//!
/*! 
    Interface that kills and cleans up all of the processes associated 
    with the target LWJs 
*/
extern flux_rc_e FLUX_control_killLWJs (
                 const flux_lwj_id_t target[], 
		 int size);

//!
/*! 
    Interface that requests for a state change from running
    to attach_requested
*/
extern flux_rc_e
FLUX_control_attachreqLWJ (
                 const flux_lwj_id_t *target);


//!
/*! 
    Interface that requests for a state change from attach_requested
    to running
*/
extern flux_rc_e
FLUX_control_attachdoneLWJ (
                 const flux_lwj_id_t *target);



extern flux_rc_e
error_log (const char *format, ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*  FLUX_API_MOCKUP_H_ */
