/*
 * $Header: $
 *--------------------------------------------------------------------------------
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC. Produced at
 * the Lawrence Livermore National Laboratory. Written by Dong H. Ahn <ahn1@llnl.gov>.
 *--------------------------------------------------------------------------------
 *
 *  Update Log:
 *        Oct 07 2013 DHA: File created
 */



#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h> 
#include <json/json.h>
#include <zmq.h>
#include <czmq.h>
#include "cmb.h"
#include "log.h"
#include "util.h"
#include "zmsg.h"
#include "flux_api.h"


typedef enum {
    level0 = 0,
    level1,
    level2,
    level3
} verbose_level_e;

#define NEW_LWJ_MSG_REQ            "job.create"
#define NEW_LWJ_MSG_REPLY          "job.create"
#define NEW_LWJ_MSG_REPLY_FIELD    "jobid"

#define JOB_STATE_RESERVED         "reserved"
#define JOB_STATE_STARTING         "starting"
#define JOB_STATE_RUNNING          "running"
#define JOB_STATE_COMPLETE         "complete"

#define JOB_STATE_KEY              "state"
#define JOB_CMDLINE_KEY            "cmdline"
#define JOB_NPROCS_KEY             "nprocs"
#define JOB_PROCTAB_KEY            "procdesc"

#define REXEC_PLUGIN_RUN_EVENT_MSG "event.rexec.run."
#define FLUXAPI_MAX_STRING         1024


/******************************************************
*
* Static Data
*
*****************************************************/
static FILE 
*myout = NULL;

static cmb_t 
cmbcxt = NULL;

static 
verbose_level_e vlevel = level0;

static char 
myhostname[FLUXAPI_MAX_STRING];

/******************************************************
*
* Static Functions
*
*****************************************************/

static int
append_timestamp ( const char *ei,const char *fstr, 
		   char *obuf, uint32_t len)
{
    int rc;
    char timelog[PATH_MAX];
    const char *format = "%b %d %T";
    time_t t;
    time(&t);
    strftime (timelog, 
	      PATH_MAX, 
	      format, 
	      localtime(&t));

    rc = snprintf(obuf, 
		  len, 
		  "<Flux API> %s (%s): %s\n", 
		  timelog, 
		  ei, 
		  fstr);
    return rc;
} 


static flux_lwj_status_e
resolve_raw_state (json_object *o)
{
    flux_lwj_status_e rc = status_null;

    json_object *val;
    if (!(json_object_object_get_ex (o,
                          "FILEVAL", &val))) {
        error_log (
            "Failed to resolve the job state",
            0);
        goto ret_loc;
    }

    const char *state_str
        = json_object_get_string (val);

    if (!state_str) {
        error_log (
            "State value is null!",
            0);
        goto ret_loc;
    }

    if ( !strcmp (state_str, JOB_STATE_RESERVED)) {
        rc = status_registered;
    }
    else if ( !strcmp (state_str, JOB_STATE_STARTING)) {
        rc = status_registered;
    }
    else if ( !strcmp (state_str, JOB_STATE_RUNNING)) {
        rc = status_running;
    }
    else if ( !strcmp (state_str, JOB_STATE_COMPLETE)) {
        rc = status_completed;
    }

    json_object_put (val);

ret_loc:
    return rc;
}


/******************************************************
*
* Public Interfaces
*
*****************************************************/

FILE *
set_log_fd (FILE *newfd)
{
    FILE *tmp = myout;
    myout = newfd;
    return tmp;
}


unsigned int
set_verbose_level (unsigned int level)
{
    int tmp = vlevel;
    vlevel = level;
    return tmp;
}


void
error_log (const char *format, unsigned int error, ...)
{    
    const char *ei_str      = error? "INFO" : "ERROR";
    char x_format[PATH_MAX] = {'\0'};
    va_list vlist;
         
    if ( ((int) vlevel) >= error) {
        append_timestamp (ei_str, format, 
            x_format, PATH_MAX);

        va_start (vlist, error);
        vfprintf (myout, x_format, vlist);
        va_end (vlist);
    }
}


flux_rc_e 
FLUX_init ()
{
    flux_rc_e rc = FLUX_OK;

    if (myout == NULL) {
        myout = stdout;
    }	

    if (gethostname (myhostname, FLUXAPI_MAX_STRING) < 0) {
	error_log (
	    "Initializing hostname failed", 0);
	rc = FLUX_ERROR;
    }

    if ( (cmbcxt = cmb_init ()) == NULL) {
	error_log (
	    "Initializing CMB (cmb_init) failed", 0);
	rc = FLUX_ERROR;
    }

    return rc;
}


flux_rc_e 
FLUX_fini ()
{
    flux_rc_e rc = FLUX_OK;

    if (!cmbcxt) {
	cmb_fini (cmbcxt);
    }
    else {
	rc = FLUX_ERROR; 
	error_log (
            "CMB never initialized?", 0);
    }

    return rc;
}


flux_rc_e 
FLUX_update_createLWJCxt (flux_lwj_id_t *lwj)
{
    int rc              = FLUX_ERROR;
    int64_t jobid       = -1;
    char *tag           = NULL;
    zmsg_t *zmsg        = NULL;
    json_object *jobreq = NULL;
    json_object *o      = NULL;


    /* Creating an empty lwj context in KVS 
     * through job plugin
     */
    jobreq = json_object_new_object ();
    if ( (rc = cmb_send_message (cmbcxt, jobreq, 
			         NEW_LWJ_MSG_REQ)) < 0) {   
	error_log (
            "Sending a cmb msg failed"
            "in FLUX_update_createLWJCxt", 0);
	goto cmb_error;
    }

    if ( (zmsg = cmb_recv_zmsg (cmbcxt, false)) == NULL ) {
	error_log (
            "Failed to receive a cmb msg"
            "in FLUX_update_createLWJCxt", 0);
	goto cmb_error;
    }

    if ( ( rc = cmb_msg_decode (zmsg, &tag, &o)) < 0) {
	error_log (
            "Failed to decode a cmb msg", 0);
	goto cmb_error;
    }

    if ( (rc = strcmp (tag, NEW_LWJ_MSG_REPLY) != 0 )) {	
	error_log (
            "Tag mismatch in FLUX_update_createLWJCxt: %s", 
            0, tag);
	goto cmb_error;
    }

    if ( (rc = util_json_object_get_int64 (o, 
                    NEW_LWJ_MSG_REPLY_FIELD, &jobid) < 0)) {
	error_log (
            "Failed to get jobid from json = %s", 0,
            json_object_to_json_string (o));		
	goto cmb_error;
    }
    
    *lwj = jobid;
    json_object_put (o);
    json_object_put (jobreq);
    zmsg_destroy (&zmsg);

    return FLUX_OK;

cmb_error:
    return rc;
}


flux_rc_e 
FLUX_update_destoryLWJCxt (const flux_lwj_id_t *lwj)
{        
    error_log ("FLUX_update_destoryLWJCxt"
	       "not implmented yet", 1); 
    return FLUX_NOT_IMPL; 
}


flux_rc_e 
FLUX_query_pid2LWJId (
                 const flux_starter_info_t *starter,
		 flux_lwj_id_t *lwj)
{
    error_log ("FLUX_update_destoryLWJCxt"
	       "not implmented yet", 1); 
    return FLUX_NOT_IMPL; 
}


flux_rc_e 
FLUX_query_LWJId2JobInfo (
	         const flux_lwj_id_t *lwj, 
		 flux_lwj_info_t *lwj_info)
{
    int netrc            = -1;
    int rc               = FLUX_OK;
    size_t gtab_size        = 0;
    flux_lwj_status_e st = status_null;
    json_object *jobj    = NULL; 
    json_object *st_jobj = NULL;
    char kvs_key[FLUXAPI_MAX_STRING] = {'\0'};

    snprintf (kvs_key,
        FLUXAPI_MAX_STRING,
        "lwj.%ld", *lwj);
    if ( (netrc = cmb_kvs_get (cmbcxt, kvs_key,
                               &jobj, KVS_GET_DIR)) < 0) {
        error_log (
            "cmb_kvs_get error", 0);
        rc = FLUX_ERROR;
        goto jkvs_error;
    }

    if (!(json_object_object_get_ex (jobj,
                               JOB_STATE_KEY,
                               &st_jobj)) ) {
        error_log (
            "Key not found? %s", 
            0, JOB_STATE_KEY);
        rc = FLUX_ERROR;
        goto jkvs_error;

    }


    st = resolve_raw_state (st_jobj);
    lwj_info->lwj = *lwj;
    lwj_info->status = st;
    lwj_info->starter.hostname = strdup (myhostname);
    lwj_info->starter.pid = -1;

    if ( (rc = FLUX_query_globalProcTableSize (
                   lwj, &gtab_size)) != FLUX_OK) {
        lwj_info->proc_table_size = gtab_size;
    }
    else {
        lwj_info->proc_table_size = 0;
    }

    json_object_put (jobj);
    json_object_put (st_jobj);

    return rc;

jkvs_error:
    return rc;
}


flux_rc_e 
FLUX_query_globalProcTableSize (
	         const flux_lwj_id_t *lwj,
		 size_t *count)
{
    flux_rc_e rc = FLUX_OK;
    char kvs_key[FLUXAPI_MAX_STRING] = {'\0'};
    json_object_iter iter;
    json_object *jobj;
    int incr = 0;
    int netrc = 0;

    /*
     * Retrieve the lwj root directory
     */
    snprintf (kvs_key,
        FLUXAPI_MAX_STRING,
        "lwj.%ld", *lwj);
    if ( (netrc = cmb_kvs_get (cmbcxt, kvs_key,
                               &jobj, KVS_GET_DIR)) < 0) {
        error_log ("kvs_get error", 0);
        rc = FLUX_ERROR;
        goto ret_loc;
    }

    json_object_object_foreachC (jobj, iter) {
        json_object *newo = json_object_object_get (
                                iter.val, "DIRVAL"); 
        if (newo) {
            /* FIXME: proctable size should be
             * stored in kvs as a field 
             */
            incr++;
        }
    }

    *count = incr;

ret_loc:
    return rc; 
}


flux_rc_e 
FLUX_query_globalProcTable (
	         const flux_lwj_id_t *lwj,
		 MPIR_PROCDESC_EXT *ptab_buf, 
		 const size_t ptab_buf_size,
                 size_t *ret_ptab_size)
{
    flux_rc_e rc = FLUX_OK;
    char kvs_key[FLUXAPI_MAX_STRING] = {'\0'};
    json_object_iter iter;
    json_object *jobj;
    int incr = 0;
    int netrc = 0;

    /*
     * Retrieve the lwj root directory
     */
    snprintf (kvs_key,
        FLUXAPI_MAX_STRING,
        "lwj.%ld", *lwj);
    if ( (netrc = cmb_kvs_get (cmbcxt, kvs_key,
                               &jobj, KVS_GET_DIR)) < 0) {
        error_log ("kvs_get error", 0);
        return FLUX_ERROR;
    }

    json_object_object_foreachC (jobj, iter) {
        json_object *newo = json_object_object_get (
                                iter.val, "DIRVAL");  
        if (newo) {
            incr++;
        }
    }

    json_object_object_foreachC (jobj, iter) {
        int rank = atoi (iter.key);
        json_object *newo 
            = json_object_object_get (
                  iter.val, "DIRVAL");  
        if (!newo) {
            continue;
        }

        if (rank < ptab_buf_size) {
            json_object *procdesc 
                = json_object_object_get(
                      newo, "procdesc");                       
            json_object *fobj;
            fobj = json_object_object_get (
                       procdesc, "FILEVAL");
            if (fobj) {
                json_object *exec_obj;
                json_object *pid_obj;
                json_object *hn_obj;

                exec_obj 
                    = json_object_object_get (
                          fobj, "command");
                pid_obj 
                    = json_object_object_get (
                          fobj, "pid");
                hn_obj 
                    = json_object_object_get (
                          fobj, "nodeid");

                if (hn_obj) {
                    ptab_buf[rank].pd.host_name 
                    = strdup (json_object_get_string (hn_obj));
                }
                else {
                    error_log (
                        "hostname unavailable for rank %d", 
                        0, rank);
                    ptab_buf[rank].pd.host_name = NULL;
                }

                if (exec_obj) {
                    ptab_buf[rank].pd.executable_name 
                        = strdup (json_object_get_string (exec_obj));
                }
                else {
                    error_log (
                        "exec name unavailable for rank %d", 
                        0, rank);
                    ptab_buf[rank].pd.executable_name = NULL;
                }

                if (pid_obj) {
                    ptab_buf[rank].pd.pid
                        = (int) (json_object_get_int64 (pid_obj));
                }
                else {
                    error_log (
                        "pid unavailable for rank %d", 
                        0, rank);
                    ptab_buf[rank].pd.pid = -1;
                }
                
                ptab_buf[rank].mpirank = rank;
                ptab_buf[rank].cnodeid = 0;
            }
            else {
                error_log (
                    "procdesc for %d ill-formed", 
                    0, rank);
            }
        }
    }
    *ret_ptab_size = incr;

    return rc; 
}


flux_rc_e 
FLUX_query_localProcTableSize (
	         const flux_lwj_id_t *lwj, 
		 const char *hostname, 
		 size_t *count)
{
    /* CMB routine to get procdesc field */
    return FLUX_NOT_IMPL;
}


flux_rc_e 
FLUX_query_localProcTable (
	         const flux_lwj_id_t *lwj,
		 const char *hostname,
		 MPIR_PROCDESC_EXT *ptab_buf, 
		 const size_t ptab_buf_size,
                 size_t *ret_ptab_size)
{
    /* CMB routine to get procdesc field */
    return FLUX_NOT_IMPL;
}


flux_rc_e 
FLUX_query_LWJStatus (
	         const flux_lwj_id_t *lwj, 
		 flux_lwj_status_e *status)
{
    flux_lwj_info_t lwjInfo;

    /*
     * TODO: perf optimization may need here
     */
    if (FLUX_query_LWJId2JobInfo (lwj, &lwjInfo) != FLUX_OK) {
        *status = lwjInfo.status;
        error_log (
            "Failed to fetch lwj info", 0);
        return FLUX_ERROR; 
    }

    *status = lwjInfo.status;    
    return FLUX_OK;
}


flux_rc_e 
FLUX_monitor_registerStatusCb (
	         const flux_lwj_id_t *lwj, 
		 int (*cback) (flux_lwj_status_e *status))
{
    return FLUX_NOT_IMPL;
}


flux_rc_e
FLUX_launch_spawn (
		 const flux_lwj_id_t *lwj, 
		 int sync, 
		 const flux_lwj_id_t *coloc_lwj,
                 const char * lwjpath,
		 char * const lwjargv[],
                 int coloc,
		 int nnodes,
		 int nproc_per_node)
{ 
    char ** argvptr;
    int netrc;
    flux_rc_e rc = FLUX_OK;
    json_object *jobj = NULL;
    json_object *state_jobj = NULL;
    json_object *proc_jobj = NULL;
    json_object *cmd_jobj = NULL;
    json_object *cmd_array = NULL;
    flux_lwj_status_e status;
    char kvs_key[FLUXAPI_MAX_STRING] = {'\0'};
    char event_msg[FLUXAPI_MAX_STRING] = {'\0'};

    /*
     * Retrieve the lwj root directory
     */
    snprintf (kvs_key, 
	FLUXAPI_MAX_STRING,
	"lwj.%ld", *lwj); 
    if ( (netrc = cmb_kvs_get (cmbcxt, kvs_key, 
                               &jobj, KVS_GET_DIR)) < 0) {
	error_log ("kvs_get error", 0);
	rc = FLUX_ERROR;
        goto ret_loc;
    }

    /*
     * Retrieve the raw job state.
     */
    if (!(json_object_object_get_ex (jobj, 
                               JOB_STATE_KEY,
                               &state_jobj)) ) {
	error_log (
            "Failed to retrieve the job state", 0);
        rc = FLUX_ERROR;
        goto ret_loc;
    }
    if ( (status = resolve_raw_state (state_jobj))
         != status_registered) {
	error_log (
            "job state (%d) isn't ready for launch", 
            0, status);
        rc = FLUX_ERROR;
        goto ret_loc;
    }

    /*
     * Put nproc_per_node
     */
    proc_jobj = json_object_new_object(); 
    json_object_object_add (proc_jobj, 
        "FILEVAL", 
        json_object_new_int (nproc_per_node));
    json_object_object_add (jobj, 
        JOB_NPROCS_KEY, 
        proc_jobj);

    /*
     * Put command line into KVS.
     */
    cmd_jobj = json_object_new_object ();
    cmd_array = json_object_new_array ();
    argvptr = (char **) lwjargv;
    while (*argvptr != NULL) {
        json_object *o = json_object_new_string (*argvptr);
        json_object_array_add (cmd_array, o);
        argvptr++;
    }
    json_object_object_add (cmd_jobj, 
        "FILEVAL", 
        cmd_array);
    json_object_object_add (jobj, 
        JOB_CMDLINE_KEY,
        cmd_jobj);

    /*
     * Put/Flush/Commit 
     */
    if (cmb_kvs_put (cmbcxt, kvs_key, jobj) < 0) {
	error_log (
            "cmb_kvs_put failed", 0);
        rc = FLUX_ERROR;
        goto ret_loc;
    }
    if (cmb_kvs_flush (cmbcxt) < 0) {
	error_log (
            "cmb_kvs_put failed", 0);
        rc = FLUX_ERROR;
        goto ret_loc;
    }
    if (cmb_kvs_commit (cmbcxt, NULL) < 0) {
	error_log (
            "cmb_kvs_put failed", 0);
        rc = FLUX_ERROR;
        goto ret_loc;
    }

    /*
     * Now KVS has all information, 
     * so tell the exec plugin to run
     */
    snprintf (event_msg,
	      FLUXAPI_MAX_STRING,
	      "%s%lu",
	      REXEC_PLUGIN_RUN_EVENT_MSG,
	      *lwj);

    if ( (netrc = cmb_event_send (cmbcxt, event_msg)) < 0 ) {
	error_log ("Sending a cmb event failed"
		   "in FLUX_launch_spawn", 0);
	rc = FLUX_ERROR;
        goto ret_loc;
    }
    
ret_loc:
    return rc;	
}


flux_rc_e 
FLUX_control_killLWJ (
                 const flux_lwj_id_t *lwj)
{
    return FLUX_NOT_IMPL;
}
