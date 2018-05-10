/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef _FLUX_CORE_JSTATCTRL_H
#define _FLUX_CORE_JSTATCTRL_H 1

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Define the job states (an abstraction independent of
 * underlying task and program execution services (RFC 8)
 * and scheduler implementation details (e.g., how the
 * attributes of a job are stored in KVS.) For more details,
 * please refer to README.md
 */
typedef enum {
    J_NULL =       0,  /*!< The state has yet to be assigned */

    /* WRECK job initial condition states:
     */
    J_RESERVED =   1, /*!< Reserved by the program execution service */
    J_SUBMITTED =  2, /*!< Submitted to the system */

    /* Scheduler internal states:
     */
    J_PENDING =   11, /*!< Pending */
    J_SCHEDREQ =  12, /*!< Resources requested to be selected */
    J_SELECTED =  13, /*!< Assigned to requested resource in RDL */
    J_ALLOCATED = 14, /*!< Got alloc/contained by the program exec service */

    /* WRECK job execution states:
     */
    J_RUNREQUEST= 21, /*!< Requested to be executed */
    J_STARTING =  22, /*!< Starting */
    J_SYNC =      23, /*!< Tasks stopped in exec waiting for a tool */
    J_RUNNING =   24, /*!< Running */
    J_COMPLETING= 26, /*!< Completing (all tasks exited, epilog running) */

    /* WRECK job terminal states:
     */
    J_CANCELLED = 51, /*!< Cancelled (before execution) */
    J_COMPLETE =  52, /*!< Completed */
    J_FAILED =    53, /*!< Failed (before exec) */

    /* Scheduler post exec states:
     */
    J_REAPED =   101, /*!< Reaped */
    J_FOR_RENT = 102, /*!< Space For Rent */
} job_state_t;

typedef int (*jsc_handler_f)(const char *base_jcb, void *arg, int errnum);

/* TODO: find a better way to manage this hierarchical
 * JCB attributes space
 */
#define JSC_JOBID "jobid"
#define JSC_STATE_PAIR "state-pair"
# define JSC_STATE_PAIR_OSTATE "ostate"
# define JSC_STATE_PAIR_NSTATE "nstate"
#define JSC_RDESC "rdesc"
# define JSC_RDESC_NNODES "nnodes"
# define JSC_RDESC_NTASKS "ntasks"
# define JSC_RDESC_NCORES "ncores"
# define JSC_RDESC_NGPUS "ngpus"
# define JSC_RDESC_WALLTIME "walltime"
#define JSC_RDL "rdl"
#define JSC_R_LITE "R_lite"
#define JSC_PDESC "pdesc"
# define JSC_PDESC_SIZE "procsize"
# define JSC_PDESC_HOSTNAMES "hostnames"
# define JSC_PDESC_EXECS "executables"
# define JSC_PDESC_PDARRAY "pdarray"
# define JSC_PDESC_RANK_PDARRAY_PID "pid"
# define JSC_PDESC_RANK_PDARRAY_HINDX "hindx"
# define JSC_PDESC_RANK_PDARRAY_EINDX "eindx"

/**
 * Register a callback to the asynchronous status change notification service.
 * "callback" will be invoked when the state of a job changes. The "jobid"
 * and "state-pair" will be passed as "base_jcb" into the callback.
 * "d" is arbitrary data that will transparently be passed into "callback."
 * However, one should pass its flux_t object as part of this callback data.
 * Note that the caller must start its reactor to get an asynchronous status
 * change notification via "callback."
 * One can register mutliple callbacks by calling this function
 * multiple times. The callbacks will be invoked in the order
 * they are registered. Returns 0 on success; otherwise -1.
 */
int jsc_notify_status (flux_t *h, jsc_handler_f callback, void *d);

/**
 * Query the "key" attribute of JCB of "jobid." The JCB info on this attribute
 * will be passed via "jcb." It is the caller's responsibility to free "jcb."
 * Returns 0 on success; otherwise -1.
 */
int jsc_query_jcb (flux_t *h, int64_t jobid, const char *key, char **jcb);


/**
 * Update the "key" attribute of the JCB of "jobid". The top-level attribute
 * of "jcb" should be identical to "key". Return 0 on success; otherwise -1.
 * This will not release "jcb," so it is the caller's responsibility to
 * free "jcb."
 */
int jsc_update_jcb (flux_t *h, int64_t jobid, const char *key, const char *jcb);


/**
 * A convenience routine (returning the internal state name correponding to "s.")
 */
const char *jsc_job_num2state (job_state_t s);
int jsc_job_state2num (const char *s);

/**
 * Accessor for nnodes, ntasks, ncores, walltime that circumvents
 * JSON encode/decode.  N.B. this is a workaround for performance issu
 * encountered when scheduling high throughput workloads.
 */
int jsc_query_rdesc_efficiently (flux_t *h, int64_t jobid,
                                 int64_t *nnodes, int64_t *ntasks,
                                 int64_t *ncores, int64_t *walltime);

#ifdef __cplusplus
}
#endif

#endif /*! _FLUX_CORE_JSTATCTRL_H */

/*
 * vi: ts=4 sw=4 expandtab
 */
