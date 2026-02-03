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
 *  An exec implementation must include the methods below (except where noted):
 *
 *   - config:  (optional) called at module load for configuration and config
 *              reload.
 *
 *   - unload:  (optional) called once at module unload
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
 *   - cancel:  cancel any pending work (i.e. shells yet to be executed)
 *
 *   - stats:   (optional) get json object of exec implementation stats
 *
 *   - active_ranks:
 *              (optional) get the set of ranks with active job shells.
 */
struct exec_implementation {
    const char *name;
    int  (*config)  (flux_t *h,
                     const flux_conf_t *conf,
                     int argc,
                     char **argv,
                     flux_error_t *errp);
    void (*unload)  (void);
    int  (*init)    (struct jobinfo *job);
    void (*exit)    (struct jobinfo *job);
    int  (*start)   (struct jobinfo *job);
    int  (*kill)    (struct jobinfo *job, int signum);
    int  (*cancel)  (struct jobinfo *job);
    json_t * (*stats) (struct jobinfo *job);
    struct idset * (*active_ranks) (struct jobinfo *job);
};

/*  Exec job information */
struct jobinfo {
    flux_t *              h;
    flux_jobid_t          id;
    char                  ns [64];   /* namespace string */
    char                * rootref;   /* ns rootref if restart */
    const flux_msg_t *    req;       /* initial request */
    uint32_t              userid;    /* requesting userid */
    int                   flags;     /* job flags */

    struct resource_set * R;         /* Fetched and parsed resource set R */
    json_t *              jobspec;   /* Fetched jobspec */

    struct idset *        critical_ranks;  /* critical shell ranks */

    uint8_t               multiuser:1;
    uint8_t               has_namespace:1;
    uint8_t               exception_in_progress:1;

    uint8_t               started:1;     /* some or all shells are starting */
    uint8_t               running:1;     /* all shells are running */
    uint8_t               finalizing:1;  /* in process of cleanup */

    int                   reattach;      /* job-manager reattach attempt */
    int                   wait_status;

    struct eventlogger *  ev;           /* event batcher */

    double                kill_timeout; /* grace time between sigterm,kill */
    flux_watcher_t       *kill_timer;
    int                   kill_count;
    flux_watcher_t       *kill_shell_timer;
    int                   kill_shell_count;

    /* Timer set when job termination begins if max-kill-timeout is
     * configured (overrides max_kill_count)
     */
    flux_watcher_t       *max_kill_timer;

    flux_watcher_t       *expiration_timer;

    double                t0;        /* timestamp we initially saw this job */

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

/* Emit  start event */
void jobinfo_started (struct jobinfo *job);

/* Emit  reattached event */
void jobinfo_reattached (struct jobinfo *job);

/* Notify job-exec that ranks in idset `ranks` have completed
 *  with the given wait status
 */
void jobinfo_tasks_complete (struct jobinfo *job,
                             const struct idset *ranks,
                             int wait_status);

/* Notify job-exec of fatal error. Triggers kill/finalize.
 */
void jobinfo_fatal_error (struct jobinfo *job, int errnum,
                          const char *fmt, ...);

void jobinfo_raise (struct jobinfo *job,
                    const char *type,
                    int severity,
                    const char *fmt, ...);

int jobinfo_drain_ranks (struct jobinfo *job,
                         const char *ranks,
                         const char *fmt,
                         ...);

/* Append a log output message to exec.eventlog for job
 */
void jobinfo_log_output (struct jobinfo *job,
                         int rank,
                         const char *component,
                         const char *stream,
                         const char *data,
                         int len);


flux_future_t *jobinfo_shell_rpc_pack (struct jobinfo *job,
                                       const char *topic,
                                       const char *fmt,
                                       ...);

/* Return the estimated amount of time the job execution system will wait
 * between first signaling a job (i.e. after an exception) until it gives
 * up and drains nodes.
 */
double job_exec_max_kill_timeout (void);

#endif /* !HAVE_JOB_EXEC_EXEC_H */

/* vi: ts=4 sw=4 expandtab
 */
