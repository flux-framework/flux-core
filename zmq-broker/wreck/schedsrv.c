/* schedsrv.c - scheduling service */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "log.h"
#include "util.h"
#include "plugin.h"

/*
 * A basic scheduling service plugin that schedules jobs to resources.
 * It waits for the resource service plugin to populate the kvs (under
 * the resrc.rank hierarchy) with the discoved resources.  Then it
 * attempts to schedule every new job submitted to the lwj kvs tree.
 *
 * Only cores are supported at this time.  Cores from more than one
 * node are grabbed if needed.  Node count requests are not supported at
 * this point.
 *
 * As jobs are submitted, the sched plugin looks for available cores
 * from the registered resources.  It then allocates what cores it
 * finds to the lwj's.  The existing wreck infrastructure is used to
 * launch the jobs.
 *
 * The sched plugin increases the associated resource's allocated core
 * count when a job begins.  When the job terminates, the allocated
 * core count is debited.

 * Jobs remain queued in the "reserved" state until they can run.
 * There is no priority.  Jobs are scheduled in order of arrival.
 * Completed jobs remain in the lwj kvs tree in the "completed" state.
 */

static char * ctime_iso8601_now (char *buf, size_t sz)
{
    struct tm tm;
    time_t now = time (NULL);

    memset (buf, 0, sz);

    if (!localtime_r (&now, &tm))
        err_exit ("localtime");
    strftime (buf, sz, "%FT%T", &tm);

    return (buf);
}

static void _wait_for_new_dir(flux_t p, char *name)
{
    kvsdir_t dir = NULL;

    if (kvs_watch_once_dir (p, &dir, name) < 0) {
        flux_log (p, LOG_ERR, "wait_for_new_dir: %s", strerror (errno));
    } else {
        flux_log (p, LOG_INFO, "wait_for_new_dir %s", kvsdir_key(dir));
        if (dir)
            kvsdir_destroy (dir);
    }
}

/*
 * _pending_job() - determine whether a job is pending.
 * IN:  path - the kvs job
 * Returns: true if the job is pending
 *          false otherwise
 */
static bool _pending_job (flux_t p, const char *path)
{
    bool rval = false;
    char *key = NULL;
    char *job_state = NULL;


    if (asprintf (&key, "%s.state", path) < 0) {
        flux_log (p, LOG_ERR, "pending_job key create failed");
    } else if (kvs_get_string (p, key, &job_state) < 0) {
        flux_log (p, LOG_ERR, "pending_job get job_state: %s",
                  strerror (errno));
    } else if (!strncmp (job_state, "pending", 8)) {
        flux_log (p, LOG_INFO, "pending_job %s", path);
        rval = true;
    }
    free (key);
    free (job_state);

    return rval;
}

/*
 * _read_alloc_cores() - Read a resource's idle cores or allocate some
 * cores
 * IN:  path - the kvs resource
 * IN:  alloc - if zero, just read the idle cores
 *              if non-zero, allocate requested cores
 * Returns:  the number of idle cores when alloc is zero
 *           the number of cores allocated when alloc is non-zero
 */
static long _read_alloc_cores (flux_t p, const char *path, long alloc)
{
    char *key = NULL;
    long alloc_cores = 0;
    long idle_cores = 0;

    if (asprintf (&key, "%s.cores", path) < 0) {
        flux_log (p, LOG_ERR, "read_alloc_cores key create failed");
    } else if (kvs_get_int64 (p, key, &idle_cores) < 0) {
        flux_log (p, LOG_ERR, "read_alloc_cores get %s: %s", key,
                  strerror (errno));
    } else {
        flux_log (p, LOG_INFO, "read_alloc_cores %s = %ld", key, idle_cores);
        free (key);

        if (asprintf (&key, "%s.alloc.cores", path) < 0) {
            flux_log (p, LOG_ERR, "read_alloc_cores key create failed");
        } else if (kvs_get_int64 (p, key, &alloc_cores) < 0) {
            flux_log (p, LOG_ERR, "read_alloc_cores get %s: %s", key,
                      strerror (errno));
        } else {
            flux_log (p, LOG_INFO, "read_alloc_cores %s = %ld", key,
                      alloc_cores);
            idle_cores -= alloc_cores;
        }
    }

    if (alloc && idle_cores) {
        if (alloc > idle_cores)
            alloc = idle_cores;
        if (kvs_put_int64 (p, key, alloc_cores + alloc) < 0) {
            flux_log (p, LOG_ERR, "read_alloc_cores put %ld alloc.cores failed",
                      alloc_cores + alloc);
        } else {
            kvs_commit(p);
            flux_log (p, LOG_INFO, "read_alloc_cores put %s = %ld", key,
                      alloc_cores + alloc);
            /* when alloc is requested, return the number allocated */
            idle_cores = alloc;
        }
    }
    free (key);

    return idle_cores;
}

/*
 * _store_cores() - write a job's allocated cores to the lwj kvs
 * IN: job - path to the lwj
 *     path - path to the allocated resource
 * Results in an entry of the form:  lwj.3.rank.2.cores = 4
 */
static void _store_cores(flux_t p, const char *job, const char *path, long alloc)
{
    char *key = NULL;

    if (asprintf (&key, "%s.rank.%s.cores", job, path) < 0) {
        flux_log (p, LOG_ERR, "store_cores key create failed");
    } else if (kvs_put_int64 (p, key, alloc) < 0) {
        flux_log (p, LOG_ERR, "store_cores put %ld cores failed", alloc);
    } else {
        kvs_commit(p);
        flux_log (p, LOG_INFO, "store_cores put %s = %ld", key, alloc);
    }
    free (key);
}

/*
 * _alloc_resrcs - allocate resources to a job
 * This includes cores from multiple ranks if needed to satisfy request.
 * IN:  job - path to the lwj in the kvs
 * IN:  cores - number of cores to allocate
 * Returns: true if all requested cores were allocated.
 */
static bool _alloc_resrcs (flux_t p, const char *job, long cores)
{
    bool rval = false;
    char *key = NULL;
    const char *name;
    kvsdir_t dir;
    kvsitr_t itr;
    long alloc_cores = 0;
    long reqd_cores = cores;

    if (kvs_get_dir (p, &dir, "resrc.rank") < 0) {
        flux_log (p, LOG_ERR, "alloc_resrcs get resrc.rank dir: %s",
                  strerror (errno));
        return false;
    }

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_isdir (dir, name)) {
            reqd_cores -= _read_alloc_cores (p, key, 0);
            if (reqd_cores <= 0) {
                rval = true;
                break;
            }
        }
        free (key);
    }

    if (rval) {
        reqd_cores = cores;
        kvsitr_rewind (itr);
        while ((name = kvsitr_next (itr))) {
            key = kvsdir_key_at (dir, name);
            if (kvsdir_isdir (dir, name)) {
                alloc_cores = _read_alloc_cores (p, key, reqd_cores);
                if (alloc_cores) {
                    _store_cores(p, job, name, alloc_cores);
                    reqd_cores -= alloc_cores;
                    if (reqd_cores == 0) {
                        break;
                    }
                }
            }
            free (key);
        }
    }

    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
    return rval;
}

/*
 * _allocate_job() - updates the state of a job's kvs entry to
 * indicate launch readiness.
 * IN: path - the job id
 * Returns true on success
 */
static bool _allocate_job (flux_t p, const char *path)
{
    bool rval = false;
    char *jobid = NULL;
    char *key = NULL;
    char *key2 = NULL;
    char buf [64];

    ctime_iso8601_now (buf, sizeof (buf));
    if ((jobid = strstr(path, "lwj.")))
        jobid += 4;

    if (asprintf (&key, "%s.state", path) < 0) {
        flux_log (p, LOG_ERR, "allocate_job key create failed");
    } else if (flux_event_send (p, NULL, "event.rexec.run.%s", jobid) < 0) {
        flux_log (p, LOG_ERR, "allocate_job event send failed: %s",
                  strerror (errno));
    } else if (kvs_put_string (p, key, "runrequest") < 0) {
        flux_log (p, LOG_ERR, "allocate_job %s state update failed: %s",
                  jobid, strerror (errno));
    } else if (asprintf (&key2, "%s.runrequest-time", path) < 0) {
        flux_log (p, LOG_ERR, "allocate_job key2 create failed");
    } else if (kvs_put_string (p, key2, buf) < 0) {
        flux_log (p, LOG_ERR, "allocate_job %s runrequest-time failed: %s",
                  jobid, strerror (errno));
    } else {
        kvs_commit(p);
        flux_log (p, LOG_INFO, "job %s runrequest", jobid);
        rval = true;
    }
    free (key);
    free (key2);

    return rval;
}

/*
 * _sched_job() - Find the resources for a job and run it if possible
 * IN:  path - path to the lwj
 * Returns true on success
 */
static bool _sched_job (flux_t p, const char *path)
{
    bool rval = false;
    char *key = NULL;
    long reqd_tasks = 0;

    flux_log (p, LOG_INFO, "sched_job %s", path);

    if (asprintf (&key, "%s.ntasks", path) < 0) {
        flux_log (p, LOG_ERR, "sched_job key create failed");
    } else if (kvs_get_int64 (p, key, &reqd_tasks) < 0) {
        flux_log (p, LOG_ERR, "sched_job get %s: %s", key, strerror (errno));
    } else if (_alloc_resrcs (p, path, reqd_tasks)) {
        flux_log (p, LOG_INFO, "sched_job %s requests %ld tasks", path,
                  reqd_tasks);
        rval = _allocate_job (p, path);
    }
    free (key);

    return rval;
}

/*
 * _sched_loop() - loop through the pending jobs and schedule what we can.
 * IN:  p - flux handle
 */
static void _sched_loop (flux_t p)
{
    char *key = NULL;
    const char *name;
    kvsdir_t dir;
    kvsitr_t itr;

    if (kvs_get_dir (p, &dir, "lwj") < 0) {
        flux_log (p, LOG_ERR, "sched_loop get lwj dir: %s", strerror (errno));
        return;
    }
    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_isdir (dir, name)) {
            if (_pending_job (p, key)) {
                if (!_sched_job(p, key)) {
                    /* There was a problem.  Let's stop for now */
                    break;
                }
            }
        }
        free (key);
    }
    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
}

/*
 * _reclaim_resrcs() - increase the idle core count of each of a job's
 * resources following the termination of a running job.
 * IN: job - the job that just terminated
 */
static void _reclaim_resrcs(flux_t p, char *job)
{
    char *key = NULL;
    char *key1 = NULL;
    char *key2 = NULL;
    const char *name;
    kvsdir_t dir;
    kvsitr_t itr;
    long alloc_cores = 0;
    long job_cores = 0;

    flux_log (p, LOG_INFO, "reclaim_resrcs %s", job);

    if (kvs_get_dir (p, &dir, "%s.rank", job) < 0) {
        flux_log (p, LOG_ERR, "reclaim_resrcs get lwj dir: %s",
                  strerror (errno));
        return;
    }

    itr = kvsitr_create (dir);
    while ((name = kvsitr_next (itr))) {
        key = kvsdir_key_at (dir, name);
        if (kvsdir_isdir (dir, name)) {
            if (asprintf (&key1, "%s.cores", key) < 0) {
                flux_log (p, LOG_ERR, "reclaim_resrcs key1 create failed");
            } else if (kvs_get_int64 (p, key1, &job_cores) < 0) {
                flux_log (p, LOG_ERR, "reclaim_resrcs get %s: %s", key1,
                          strerror (errno));
            } else if (asprintf (&key2, "resrc.rank.%s.alloc.cores", name) < 0) {
                flux_log (p, LOG_ERR, "reclaim_resrcs key2 create failed");
            } else if (kvs_get_int64 (p, key2, &alloc_cores) < 0) {
                flux_log (p, LOG_ERR, "reclaim_resrcs get %s: %s", key2,
                          strerror (errno));
            } else {
                alloc_cores -= job_cores;
                if (kvs_put_int64 (p, key2, alloc_cores) < 0) {
                    flux_log (p, LOG_ERR,
                              "reclaim_resrcs put %ld alloc.cores failed",
                              alloc_cores);
                } else {
                    kvs_commit(p);
                    flux_log (p, LOG_INFO, "reclaim_resrcs put %s = %ld", key2,
                              alloc_cores);
                }
            }
            free (key2);
            free (key1);
        }
        free (key);
    }

    kvsitr_destroy (itr);
    kvsdir_destroy (dir);
}

/*
 * _new_job_state() - callback for when a job state change is detected
 * If the job just became pending, try to schedule it
 * If the job just terminated, reclaim its resources and kick off a
 * scheduling loop
 * IN:  key - key to the new job state
 * IN:  dir - kvsdir handle
 * IN:  arg - flux handle
 * IN:  errnum - error number
 */
static void _new_job_state (const char *key, kvsdir_t dir, void *arg, int errnum)
{
    char *job_state = NULL;
    char *key2 = NULL;
    char *req_time = NULL;
    flux_t p = (flux_t)arg;

    if (errnum > 0) {
        flux_log (p, LOG_ERR, "new_job_state %s: %s", key, strerror (errnum));
    } else if (kvs_get_string (p, key, &job_state) < 0) {
        flux_log (p, LOG_ERR, "new_job_state %s: %s", key, strerror (errno));
    } else if (!strncmp (job_state, "pending", 7)) {
        flux_log (p, LOG_INFO, "new_job_state %s: %s", key, job_state);
        _sched_loop (p);
    } else if (!strncmp (job_state, "complete", 8)) {
        char *job = strdup(key);
        char *ptr = strstr(job, ".state");

        if (ptr) {
            *ptr = '\0';
            if (asprintf (&key2, "%s.runrequest-time", job) < 0) {
                flux_log (p, LOG_ERR, "new_job_state key2 create failed");
            } else if (kvs_get_string (p, key2, &req_time) == 0) {
                _reclaim_resrcs(p, job);
                if (kvs_put_string (p, key, "reaped") < 0) {
                    flux_log (p, LOG_ERR,
                              "new_job_state %s state update failed: %s", job,
                              strerror (errno));
                } else {
                    kvs_commit(p);
                    flux_log (p, LOG_INFO, "job %s reaped", job);
                    _sched_loop (p);
                }
            } else {
                flux_log (p, LOG_INFO, "new_job_state ignored %s", job);
            }
            free (key2);
        }
        free (job);
    }
}

/*
 * _new_job() - callback for when a new job is submitted to the lwj kvs
 * IN:  key - key to the new job id
 * IN:  dir - kvsdir handle
 * IN:  arg - flux handle
 * IN:  errnum - error number
 */
static void _new_job (const char *key, kvsdir_t dir, void *arg, int errnum)
{
    char *key2;
    flux_t p = (flux_t)arg;
    long jobid;

    if (errnum > 0) {
        flux_log (p, LOG_ERR, "%s: %s", key, strerror (errnum));
    } else if (kvs_get_int64 (p, key, &jobid) < 0) {
        flux_log (p, LOG_ERR, "new_job get %s: %s", key, strerror (errno));
    } else if (asprintf (&key2, "lwj.%lu.state", jobid - 1) < 0) {
        flux_log (p, LOG_ERR, "new_job key create failed");
    } else if (kvs_watch_string (p, key2, (KVSSetStringF*)_new_job_state, p)
               < 0) {
        flux_log (p, LOG_ERR, "watch lwj state: %s", strerror (errno));
    } else {
        flux_log (p, LOG_INFO, "job ID %ld submitted", jobid - 1);
    }
}

/*
 * plugin entry point
 */
int mod_main (flux_t p, zhash_t *args)
{
    flux_log_set_facility (p, "sched");
    flux_log (p, LOG_INFO, "sched plugin starting");

    /* Wait for resource info to appear */
    _wait_for_new_dir(p, "resrc.rank");

    /* Resister for job info */
    _wait_for_new_dir(p, "lwj");

    if (kvs_watch_int64 (p, "lwj.next-id", (KVSSetInt64F*)_new_job, p) < 0) {
        flux_log (p, LOG_ERR, "watch lwj.next-id: %s", strerror (errno));
    } else {
        flux_log (p, LOG_INFO, "sched plugin initialized");
    }
    if (flux_reactor_start (p) < 0) {
        flux_log (p, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
