#ifndef _FLUX_JOB_MANAGER_JOB_H
#define _FLUX_JOB_MANAGER_JOB_H

#include "src/common/libjob/job.h"

struct job {
    flux_jobid_t id;
    uint32_t userid;
    int priority;
    double t_submit;
    int flags;

    void *list_handle;  // private to queue.c
    int refcount;       // private to job.c
};

void job_decref (struct job *job);
struct job *job_incref (struct job *job);

struct job *job_create (flux_jobid_t id,
                        int priority,
                        uint32_t userid,
                        double t_submit,
                        int flags);

#endif /* _FLUX_JOB_MANAGER_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

