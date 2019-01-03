/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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

extern const char *const JSC_JOBID;
extern const char *const JSC_STATE_PAIR;
extern const char *const  JSC_STATE_PAIR_OSTATE;
extern const char *const  JSC_STATE_PAIR_NSTATE;
extern const char *const JSC_RDESC;
extern const char *const  JSC_RDESC_NNODES;
extern const char *const  JSC_RDESC_NTASKS;
extern const char *const  JSC_RDESC_NCORES;
extern const char *const  JSC_RDESC_NGPUS;
extern const char *const  JSC_RDESC_WALLTIME;
extern const char *const JSC_RDL;
extern const char *const JSC_R_LITE;

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
 * JSON encode/decode.  N.B. this is a workaround for performance issue
 * encountered when scheduling high throughput workloads.
 */
int jsc_query_rdesc_efficiently (flux_t *h, int64_t jobid,
                                 int64_t *nnodes, int64_t *ntasks,
                                 int64_t *ncores, int64_t *walltime,
                                 int64_t *ngpus);

#ifdef __cplusplus
}
#endif

#endif /*! _FLUX_CORE_JSTATCTRL_H */

/*
 * vi: ts=4 sw=4 expandtab
 */
