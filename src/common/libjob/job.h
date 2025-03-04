/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_JOB_H
#define _FLUX_CORE_JOB_H

#include <stdbool.h>
#include <stdint.h>
#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

enum job_submit_flags {
    FLUX_JOB_PRE_SIGNED = 1,    // 'jobspec' is already signed
    FLUX_JOB_DEBUG = 2,
    FLUX_JOB_WAITABLE = 4,      // flux_job_wait() will be used on this job
    FLUX_JOB_NOVALIDATE = 8,    // don't validate jobspec (instance owner only)
};

enum job_event_watch_flags {
    FLUX_JOB_EVENT_WATCH_WAITCREATE = 1, // wait for path to exist
};

enum job_lookup_flags {
    /* return special fields as decoded JSON objects instead of strings
     * - currently works for jobspec and R
     */
    FLUX_JOB_LOOKUP_JSON_DECODE = 1,
    /* get current value of special fields by applying eventlog
     * updates for those fields
     * - currently works for jobspec and R
     */
    FLUX_JOB_LOOKUP_CURRENT = 2,
};

enum job_urgency {
    FLUX_JOB_URGENCY_MIN = 0,
    FLUX_JOB_URGENCY_HOLD = FLUX_JOB_URGENCY_MIN,
    FLUX_JOB_URGENCY_DEFAULT = 16,
    FLUX_JOB_URGENCY_MAX = 31,
    FLUX_JOB_URGENCY_EXPEDITE = FLUX_JOB_URGENCY_MAX,
};

enum job_queue_priority {
    FLUX_JOB_PRIORITY_MIN = 0,
    FLUX_JOB_PRIORITY_MAX = 4294967295,
};

// N.B. value is duplicated in python bindings
#define FLUX_JOBID_ANY 0xFFFFFFFFFFFFFFFF // ~(uint64_t)0

typedef enum {
    FLUX_JOB_STATE_NEW                    = 1,
    FLUX_JOB_STATE_DEPEND                 = 2,
    FLUX_JOB_STATE_PRIORITY               = 4,
    FLUX_JOB_STATE_SCHED                  = 8,
    FLUX_JOB_STATE_RUN                    = 16,
    FLUX_JOB_STATE_CLEANUP                = 32,
    FLUX_JOB_STATE_INACTIVE               = 64,   // captive end state
} flux_job_state_t;

#define FLUX_JOB_NR_STATES 7

/* Virtual states, for convenience.
 */
enum {
    /* FLUX_JOB_STATE_DEPEND | FLUX_JOB_STATE_PRIORITY | FLUX_JOB_STATE_SCHED */
    FLUX_JOB_STATE_PENDING    = 14,
    /* FLUX_JOB_STATE_RUN | FLUX_JOB_STATE_CLEANUP */
    FLUX_JOB_STATE_RUNNING    = 48,
    /* FLUX_JOB_STATE_PENDING | FLUX_JOB_STATE_RUNNING */
    FLUX_JOB_STATE_ACTIVE     = 62,
};

/* Result of a job
 */
typedef enum {
    FLUX_JOB_RESULT_COMPLETED = 1,
    FLUX_JOB_RESULT_FAILED = 2,
    FLUX_JOB_RESULT_CANCELED = 4,
    FLUX_JOB_RESULT_TIMEOUT = 8,
} flux_job_result_t;

typedef uint64_t flux_jobid_t;

/*  Parse a jobid from NULL-terminated string 's' in any supported encoding.
 *  Returns 0 on success, -1 on failure.
 */
int flux_job_id_parse (const char *s, flux_jobid_t *id);

/*  Encode a jobid into encoding "type", writing the result to buffer
 *   buf of size bufsz.
 *  Supported encoding types include:
 *   "dec", "hex", "kvs", "dothex", "words", "f58", or "f58plain"
 *  Returns 0 on success, -1 on failure with errno set:
 *   EPROTO: Invalid encoding type
 *   EINVAL: Invalid other argument
 */
int flux_job_id_encode (flux_jobid_t id, const char *type,
                        char *buf, size_t bufsz);

/* Convert state to string.  'fmt' may be:
 * "s" - lower case short name
 * "S" - upper case short name
 * "l" - lower case long name
 * "L" - upper case long name
 * This function always returns a valid string, though it may
 * be something like "(unknown)".
 */
const char *flux_job_statetostr (flux_job_state_t state, const char *fmt);

/* Convert state string in any of the forms produced by flux_job_statetostr()
 * back to state.  Returns 0 on success, -1 on failure with errno set.
 */
int flux_job_strtostate (const char *s, flux_job_state_t *state);

const char *flux_job_resulttostr (flux_job_result_t result, const char *fmt);

int flux_job_strtoresult (const char *s, flux_job_result_t *result);

/* Submit a job to the system.
 * 'jobspec' should be RFC 14 jobspec.
 * 'urgency' should be a value from 0 to 31 (16 if not instance owner).
 * 'flags' should be 0 for now.
 * The system assigns a jobid and returns it in the response.
 */
flux_future_t *flux_job_submit (flux_t *h, const char *jobspec,
                                int urgency, int flags);

/* Parse jobid from response to flux_job_submit() request.
 * Returns 0 on success, -1 on failure with errno set - and an extended
 * error message may be available with flux_future_error_string().
 */
int flux_job_submit_get_id (flux_future_t *f, flux_jobid_t *id);

/* Wait for jobid to enter INACTIVE state.
 * If jobid=FLUX_JOBID_ANY, wait for the next waitable job.
 * Fails with ECHILD if there is nothing to wait for.
 */
flux_future_t *flux_job_wait (flux_t *h, flux_jobid_t id);
int flux_job_wait_get_status (flux_future_t *f,
                              bool *success,
                              const char **errstr);
int flux_job_wait_get_id (flux_future_t *f, flux_jobid_t *id);

FLUX_DEPRECATED(flux_future_t *flux_job_list (flux_t *,
                                              int,
                                              const char *,
                                              uint32_t,
                                              int));

FLUX_DEPRECATED(flux_future_t *flux_job_list_inactive (flux_t *,
                                                       int,
                                                       double,
                                                       const char *));

/* Similar to flux_job_list(), but retrieve job info for a single
 * job id */
flux_future_t *flux_job_list_id (flux_t *h,
                                 flux_jobid_t id,
                                 const char *attrs_json_str);

/* Raise an exception for job.
 * Severity is 0-7, with severity=0 causing the job to abort.
 * Note may be NULL or a human readable message.
 */
flux_future_t *flux_job_raise (flux_t *h, flux_jobid_t id,
                               const char *type, int severity,
                               const char *note);

/* Cancel a job.
 * Reason may be NULL or a human readable message.
 */
flux_future_t *flux_job_cancel (flux_t *h, flux_jobid_t id, const char *reason);

/* Deliver a signal to a job.
 */
flux_future_t *flux_job_kill (flux_t *h, flux_jobid_t id, int signum);

/* Change job urgency.
 */
flux_future_t *flux_job_set_urgency (flux_t *h, flux_jobid_t id, int urgency);

/* Write KVS path to 'key' relative to job directory for job 'id'.
 * If key=NULL, write the job directory.
 * Returns string length on success, or -1 on failure.
 */
int flux_job_kvs_key (char *buf, int bufsz, flux_jobid_t id, const char *key);

/* Same as above but construct key relative to job guest directory,
 * and if FLUX_KVS_NAMESPACE is set, assume guest is the root directory.
 */
int flux_job_kvs_guest_key (char *buf,
                            int bufsz,
                            flux_jobid_t id,
                            const char *key);


/* Write KVS job namespace name to to buffer 'buf'.
 * Returns string length on success or < 0 on failure.
 */
int flux_job_kvs_namespace (char *buf,
                            int bufsz,
                            flux_jobid_t id);

/* Job eventlog watch functions
 * - path specifies optional alternate eventlog path
 */
flux_future_t *flux_job_event_watch (flux_t *h, flux_jobid_t id,
                                     const char *path, int flags);
int flux_job_event_watch_get (flux_future_t *f, const char **event);
int flux_job_event_watch_cancel (flux_future_t *f);

/*  Wait for job to reach its terminal state and fetch the job result
 *   along with other ancillary information from the job eventlog.
 */
flux_future_t *flux_job_result (flux_t *h, flux_jobid_t id, int flags);

/*  Get the job result "payload" as a JSON string
 */
int flux_job_result_get (flux_future_t *f,
                         const char **json_str);

/*  Decode and unpack the result payload from future `f`.
 *  The result object contains the following information:
 *
 *   {
 *     "id":i,                 jobid
 *     "result:i,              flux_job_result_t
 *     "t_submit":f,           timestamp of job submit event
 *     "t_run":f,              timestamp of job alloc event
 *     "t_cleanup":f,          timestamp of job finish event
 *     "waitstatus"?i,         wait status (if job started)
 *     "exception_occurred":b, true if job exception occurred
 *     "exception_severity"?i, exception severity (if exception)
 *     "exception_type"?s,     exception type (if exception)
 *     "exception_note"?s      exception note (if exception)
 *   }
 *
 */
int flux_job_result_get_unpack (flux_future_t *f, const char *fmt, ...);


/*  Get remaining time in floating point seconds for the current job or
 *  enclosing instance (i.e., if the current process is associated with
 *  a flux instance, but is not part of a parallel job).
 *
 *  Returns 0 on success with timeleft assigned to the remaining time.
 *  If there is no expiration in the current context (e.g. the job has
 *  no timelimit), then timeleft is set to infinity. If the job is not
 *  in RUN state, or the job has expired, then timeleft is set to 0.
 *
 *  Returns -1 with error string assigned to 'errp' on failure.
 */
int flux_job_timeleft (flux_t *h, flux_error_t *errp, double *timeleft);

/*  Unwrap signed data to a NUL terminated string, e.g. J -> jobspec.
 *
 *  If verify is true, then fail if signing mechanism is invalid or
 *   signing user does not match current uid. On failure, error.text
 *   is filled in with an error message. (errno not necessarily
 *   guaranteed to be valid).
 *
 *  Works when flux-core is built with or without flux-security
 *
 *  Caller must free returned value if non-NULL.
 *
 *  Predominantly used by flux-core internally and associated
 *  libraries/tools.
 */
char *flux_unwrap_string (const char *in,
                          bool verify,
                          uint32_t *userid,
                          flux_error_t *error);


/* Convert the waitstatus from a job `finish` event to an exit code.
 * If the job exited with nonzero status, then place an appropriate error
 * message in errp->text.
 */
int flux_job_waitstatus_to_exitcode (int waitstatus, flux_error_t *errp);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
