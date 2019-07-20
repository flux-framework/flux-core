/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_H
#define HAVE_JOB_EXEC_H 1

#include <jansson.h>
#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libutil/fluid.h"
#include "rset.h"

struct job_exec_ctx;
struct jobinfo;

/*  Exec implementation interface:
 *
 *  An exec implementation must include the methods below:
 *
 *   - init:    allow selection and initialization of an exec implementation
 *              from jobspec and/or R. An implementation should return 0 to
 *              "pass" on handling this job, > 0 to denote successful
 *              initialization, or < 0 for a fatal error (generates immediate
 *              exception).
 *
 *   - exit:    called at job destruction so implementation may free resources
 *
 *   - start:   start execution of job
 *
 *   - kill:    signal executing job shells, e.g. due to exception or other
 *              fatal error.
 *
 *   - cleanup: Initiate any cleanup (epilog) on ranks.
 */
struct exec_implementation {
    const char *name;
    int  (*init)    (struct jobinfo *job);
    void (*exit)    (struct jobinfo *job);
    int  (*start)   (struct jobinfo *job);
    int  (*kill)    (struct jobinfo *job, int signum);
    int  (*cleanup) (struct jobinfo *job, const struct idset *ranks);
};

/*  Exec job information */
struct jobinfo {
    flux_t *              h;
    flux_jobid_t          id;
    char                  ns [64];   /* namespace string */
    flux_msg_t *          req;       /* copy of initial request */
    uint32_t              userid;    /* requesting userid */
    int                   flags;     /* job flags */

    struct resource_set * R;         /* Fetched and parsed resource set R */
    json_t *              jobspec;   /* Fetched jobspec */

    uint8_t               needs_cleanup:1;
    uint8_t               has_namespace:1;
    uint8_t               exception_in_progress:1;
    uint8_t               running:1;
    uint8_t               finalizing:1;

    int                   wait_status;

    double                kill_timeout; /* grace time between sigterm,kill */
    flux_watcher_t       *kill_timer;

    /* Exec implementation for this job */
    struct exec_implementation *impl;
    void *                      data;

    /* Private job-exec module data */
    int                   refcount;
    struct job_exec_ctx * ctx;
};

void jobinfo_incref (struct jobinfo *job);
void jobinfo_decref (struct jobinfo *job);

/* Emit an event to the exec.eventlog */
int jobinfo_emit_event_pack_nowait (struct jobinfo *job,
                                     const char *name,
                                     const char *fmt, ...);

/* Emit  start event with optional note in jansson pack format */
void jobinfo_started (struct jobinfo *job, const char *fmt, ...);

/* Notify job-exec that ranks in idset `ranks` have completed
 *  with the given wait status
 */
void jobinfo_tasks_complete (struct jobinfo *job,
                             const struct idset *ranks,
                             int wait_status);

/* Notify job-exec that ranks in idset `ranks` have finished epilog,
 *  and resources can be released
 */
void jobinfo_cleanup_complete (struct jobinfo *job,
                               const struct idset *ranks,
                               int rc);


/* Notify job-exec of fatal error. Triggers kill/cleanup.
 */
void jobinfo_fatal_error (struct jobinfo *job, int errnum,
                          const char *fmt, ...);

#endif /* !HAVE_JOB_EXEC_EXEC_H */

/* vi: ts=4 sw=4 expandtab
 */
