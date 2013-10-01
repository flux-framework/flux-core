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


#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include "flux_api_mockup.h"

#define FLUX_MAX_NUM_LWJ 10000
#define FLUX_INIT_ID     -1
#define FLUX_ID_START    100
#define FLUX_ID_RANGE    1000
#define FLUX_MAX_ID      FLUX_ID_START + FLUX_ID_RANGE

/******************************************************
 *
 * Static Data
 *
 *****************************************************/
static flux_lwj_info_t lwjArray[FLUX_MAX_NUM_LWJ];
static int idCounter  = FLUX_ID_START;  
static int pidCounter = FLUX_ID_START;  
static int curLWJSlot = 0;
static FILE *myout = NULL;


/******************************************************
 *
 * Static Functions
 *
 *****************************************************/

inline int getAndIncLWJid()
{
    int retId;
    if (idCounter >= FLUX_MAX_ID) {
	idCounter = FLUX_ID_START;
    }
    retId = idCounter;
    idCounter++;
    return retId;	
}


inline int getAndIncPid()
{
    int retId;
    if (pidCounter >= FLUX_MAX_ID) {
	pidCounter = FLUX_ID_START;
    }
    retId = pidCounter;
    pidCounter++;
    return retId;	
}


inline int getAndIncLWJSlot()
{
    int retSlot;
    if (curLWJSlot >= FLUX_MAX_NUM_LWJ) {
	curLWJSlot = 0;
    }
    retSlot = curLWJSlot;
    curLWJSlot++;
    return retSlot;	
}


static flux_rc_e
FLUX_getLWJEntry (
		 const flux_lwj_id_t *lwj, 
		 flux_lwj_info_t **lwjInfo)
{
    int i;
    *lwjInfo = NULL;
    for (i=0; i < FLUX_MAX_NUM_LWJ; ++i) {
        if (lwj->id == lwjArray[i].lwj.id) {
            *lwjInfo = &lwjArray[i];
            break;
        }
    }

    return (*lwjInfo == NULL)? FLUX_ERROR : FLUX_OK;
}


/******************************************************
 *
 * Public Functions
 *
 *****************************************************/

flux_rc_e
error_log (const char *format, ...)
{
    int rc;
    va_list vlist;
    char x_format[1024];

    snprintf (x_format, 
              1024, 
              "<FLUX API> %s\n", 
              format);

    va_start (vlist, format);
    rc = vfprintf (myout, x_format, vlist);
    va_end (vlist);

    return rc;
}


flux_rc_e
FLUX_init ()
{
    int i;
    myout = stdout;

    for (i=0; i < FLUX_MAX_NUM_LWJ; ++i) {
	lwjArray[i].lwj.id = FLUX_INIT_ID;
	lwjArray[i].status = status_null;
        lwjArray[i].pid = FLUX_INIT_ID;
        lwjArray[i].hn = NULL;
	lwjArray[i].procTableSize = 0; 
        lwjArray[i].procTable = NULL;
    }
    
    /*
     * MOCKUP for a running lwj
     */
    int s = getAndIncLWJSlot();
    char hn[128];
    gethostname (hn, 128);
    lwjArray[s].lwj.id = getAndIncLWJid();
    lwjArray[s].status = FLUX_MOCKUP_STATUS;
    lwjArray[s].hn = strdup (hn);
    lwjArray[s].pid = FLUX_MOCKUP_PID; 
    lwjArray[s].procTableSize = 1; 
    lwjArray[s].procTable 
        = (MPIR_PROCDESC_EXT *) malloc (sizeof (MPIR_PROCDESC_EXT));
    lwjArray[s].procTable[0].pd.executable_name 
        = strdup (FLUX_MOCKUP_EXEC);  
    lwjArray[s].procTable[0].pd.host_name
        = strdup (hn);  
    lwjArray[s].procTable[0].pd.pid 
        = FLUX_MOCKUP_PID;
    lwjArray[s].procTable[0].mpirank = 0;
    lwjArray[s].procTable[0].cnodeid = 0;

    return FLUX_OK;
}


flux_rc_e
FLUX_update_createLWJCxt (
	         flux_lwj_id_t *lwj)
{
    int s = getAndIncLWJSlot();
    lwjArray[s].lwj.id = getAndIncLWJid();
    lwjArray[s].status = status_registered;
    lwjArray[s].hn = NULL; 
    lwjArray[s].pid = FLUX_INIT_ID;
    lwjArray[s].procTableSize = 0;
    lwjArray[s].procTable = NULL;
    
    memcpy((void *) lwj, 
	   (void *) &lwjArray[s], 
	   sizeof (flux_lwj_id_t));

    return FLUX_OK;
}


flux_rc_e 
FLUX_update_destoryLWJCxt (
                 flux_lwj_id_t *lwj)
{
    flux_rc_e rc = FLUX_OK;
    flux_lwj_info_t *entry;

    rc = FLUX_getLWJEntry (lwj, &entry);
    if (rc != FLUX_OK) 
        goto ret_loc;

    entry->lwj.id = FLUX_INIT_ID;
    entry->status = status_null;
    if (entry->hn) {
       free (entry->hn); 
       entry->hn = NULL;
    }
    entry->pid = FLUX_INIT_ID;
    entry->procTableSize = 0;
    if (entry->procTable) {
	free (entry->procTable); 
	entry->procTable = NULL; 
    }

ret_loc:
    return rc;
}


flux_rc_e
FLUX_query_pid2LWJId (
	         const char *hn, 
		 pid_t pid, 
		 flux_lwj_id_t *lwj)
{
    int i=0; 
    int match = 0;

    for (i=0; i < FLUX_MAX_NUM_LWJ; ++i) {
	if (lwjArray[i].pid == pid) {
	    if ( (lwjArray[i].hn != NULL)
		 && (hn != NULL) ) {
		if (strcmp (lwjArray[i].hn, 
			    hn) == 0) {
		    match = 1;
		    break;
		}
	    }
	    else if (lwjArray[i].hn == NULL) {
		match = 1;	
		break;
	    }
            else {
                error_log ("No matching lwj found");
            }
	}
    }

    if ( match == 1 ) {
	memcpy((void *) lwj, 
	       (void *)&lwjArray[i],
	       sizeof (flux_lwj_id_t));
    }
	
    return (match == 1)? FLUX_OK : FLUX_ERROR;    
}


flux_rc_e
FLUX_query_LWJId2JobInfo (
	         const flux_lwj_id_t *lwj, 
		 flux_lwj_info_t *info)
{
    flux_lwj_info_t *entry = NULL;
    flux_rc_e rc;

    rc = FLUX_getLWJEntry (lwj, &entry);
    if (rc == FLUX_OK) {
	memcpy ((void *) info, 
		(void *) entry, 
		sizeof(flux_lwj_info_t));
    }
   
    return rc;
}


flux_rc_e
FLUX_query_globalProcTableSize (
	         const flux_lwj_id_t *lwj,
		 size_t *count)
{
    flux_rc_e rc = FLUX_OK;
    flux_lwj_info_t *entry = NULL;

    if (FLUX_getLWJEntry (lwj, &entry) == FLUX_OK) {
	*count = entry->procTableSize;
    }
    else {
	rc = FLUX_ERROR;
	*count = 0;
    }

    return rc;
}


flux_rc_e
FLUX_query_localProcTableSize (
	         flux_lwj_id_t *lwj, 
		 const char *hn, 
		 size_t *count)
{
    return FLUX_query_globalProcTableSize (lwj, count);
}


flux_rc_e FLUX_query_globalProcTable (
	         const flux_lwj_id_t *lwj,
		 MPIR_PROCDESC_EXT *pt, 
		 size_t count)
{
    int i;
    flux_rc_e rc = FLUX_OK;
    flux_lwj_info_t *entry = NULL;

    rc = FLUX_getLWJEntry (lwj, &entry);

    if ((rc != FLUX_OK)
	|| (count != entry->procTableSize)) {
	return rc;
    }

    for (i=0; i < count; ++i) {
	pt[i].pd.host_name 
	    = strdup(entry->procTable[i].pd.host_name);
	pt[i].pd.executable_name
	    = strdup(entry->procTable[i].pd.executable_name);
	pt[i].pd.pid = entry->procTable[i].pd.pid;
	pt[i].mpirank = entry->procTable[i].mpirank;
	pt[i].cnodeid = entry->procTable[i].cnodeid;
    }
    
    return rc;
}


flux_rc_e FLUX_query_localProcTable (
	         const flux_lwj_id_t *lwj,
		 const char *hn,
		 MPIR_PROCDESC_EXT *pt, 
		 size_t count)
{
    return FLUX_query_globalProcTable 
	       (lwj, pt, count);
}


flux_rc_e
FLUX_query_LWJStatus (
	         flux_lwj_id_t *lwj, 
		 int *status)
{
    int i;
    int entryFound = 0;
    flux_lwj_info_t *entry = NULL;
    for (i=0; i < FLUX_MAX_NUM_LWJ; ++i) {
	if (FLUX_getLWJEntry (lwj, &entry) == FLUX_OK) {
	    break;
	}
    }

    if ( (i < FLUX_MAX_NUM_LWJ) && (entry != NULL) ) {
	*status = entry->status;
	entryFound = 1;
    }
    else {
        error_log ("No matching lwj found");
    }

    return (entryFound == 1) ? FLUX_OK : FLUX_ERROR;
}


flux_rc_e
FLUX_monitor_registerStatusCb (
	         const flux_lwj_id_t *lwj, 
		 int (*cb) (int *status))
{
    /* NOOP */
    return FLUX_OK;
}


flux_rc_e
FLUX_launch_spawn (
		 const flux_lwj_id_t *me, 
		 int sync, 
		 const flux_lwj_id_t *target,
                 const char *lwjpath,
		 char * const lwjargv[],
                 int coloc,
		 int nn,
		 int np /*...*/)
{
    flux_rc_e rc = FLUX_ERROR;

    if (nn > 1) {
        error_log ("Node count (%d)"
            " larger than 1 is not yet supported.",
            nn );
        goto ret_loc;
    } 

    if (coloc == 1) {
        /*
         * Co-location spawning
         */  
        if (target == NULL) {
            error_log ("Target lwj not given.");
            goto ret_loc;
        }
        else {
           if (np > 0) { 
               if (target == NULL) {
                   error_log ("co-loc with > 1 not supported.");
                   goto ret_loc;
               }
               else {
		   char hname[128];				   
                   flux_lwj_info_t *entry = NULL;
                   FLUX_getLWJEntry (me, &entry);
		   entry->procTableSize = np;
                   entry->procTable
                       = (MPIR_PROCDESC_EXT *)
                         malloc (np * sizeof (MPIR_PROCDESC_EXT));

		   gethostname (hname, 128);

                   pid_t retPid = fork();                   
                   if (retPid == 0) {
                       execv (lwjpath, lwjargv);
                   }
                   else {
	               if (sync) {
#if 0
			  usleep(1000);
                          kill (retPid, SIGSTOP);
                          kill (retPid, SIGSTOP);
#endif
		       }
                       entry->procTable[0].pd.executable_name
                           = strdup (lwjpath);
                       entry->procTable[0].pd.host_name
                           = strdup (hname);
                       entry->procTable[0].pd.pid
                           = retPid;
                       entry->procTable[0].mpirank = 0;
                       entry->procTable[0].cnodeid = 0;
                   }
                   entry->status = (sync == 1) 
                                   ? status_spawned_stopped
                                   : status_spawned_running;
		   rc = FLUX_OK;
               }
            }
        }
    }
    else {
        /*
         * Normal LWJ Spawning
         */  
        int i;
        flux_lwj_info_t *entry = NULL;
	char hname[128];
        FLUX_getLWJEntry (me, &entry);
	entry->procTableSize = np;
        entry->procTable
            = (MPIR_PROCDESC_EXT *) 
              malloc (np * sizeof (MPIR_PROCDESC_EXT));
         
	gethostname (hname, 128);

        for (i = 0; i < np; ++i) {
            pid_t retPid = fork ();
        
            if (retPid == 0) {
                /* child */
                execv (lwjpath, lwjargv);
            }
            else {
                if (sync) {
#if 0
                    usleep (1000);
		    kill (retPid, SIGSTOP);
		    kill (retPid, SIGSTOP);
#endif
                }
                entry->procTable[i].pd.executable_name
                    = strdup (lwjpath);
                entry->procTable[i].pd.host_name
                    = strdup (hname);
                entry->procTable[i].pd.pid
                    = retPid;
                entry->procTable[i].mpirank = i;
                entry->procTable[i].cnodeid = i;
            }
        }   
        entry->status = (sync == 1)
                        ? status_spawned_stopped 
                        : status_spawned_running;
	rc = FLUX_OK;
    }

ret_loc:
    return rc;
}


flux_rc_e
FLUX_control_killLWJs (
		 const flux_lwj_id_t target[], 
		 int size)
{
    /* logic to flll in */
    return FLUX_OK;
}


flux_rc_e
FLUX_control_attachreqLWJ (
		 const flux_lwj_id_t *target) 
{
    flux_rc_e rc = FLUX_ERROR;
    flux_lwj_info_t *entry = NULL;
    FLUX_getLWJEntry (target, &entry);
    if (entry->status == status_running) {
        entry->status = status_attach_requested;
        rc = FLUX_OK;
    } 

    return FLUX_OK;
}


flux_rc_e
FLUX_control_attachdoneLWJ (
                 const flux_lwj_id_t *target)
{
    flux_rc_e rc = FLUX_ERROR;
    flux_lwj_info_t *entry = NULL;
    FLUX_getLWJEntry (target, &entry);
    if (entry->status == status_attach_requested) {
        entry->status = status_running;
        rc = FLUX_OK;
    }

    return FLUX_OK;
}
