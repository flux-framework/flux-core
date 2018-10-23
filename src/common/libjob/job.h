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
};

enum job_priority {
    FLUX_JOB_PRIORITY_MIN = 0,
    FLUX_JOB_PRIORITY_DEFAULT = 16,
    FLUX_JOB_PRIORITY_MAX = 31,
};

typedef uint64_t flux_jobid_t;

/* Submit a job to the system.
 * 'jobspec' should be RFC 14 jobspec.
 * The system assigns a jobid and returns it in the response.
 */
flux_future_t *flux_job_submit (flux_t *h, const char *jobspec,
                                int priority, int flags);

/* Parse jobid from response to flux_job_submit() request.
 * Returns 0 on success, -1 on failure with errno set - and an extended
 * error message may be available with flux_future_error_string().
 */
int flux_job_submit_get_id (flux_future_t *f, flux_jobid_t *id);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
