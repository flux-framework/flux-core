/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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

/*
 * Update Log:
 *       May 24 2012 DHA: File created.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <czmq.h>
#include <dlfcn.h>

#include "util.h"
#include "log.h"
#include "shortjson.h"
#include "plugin.h"
#include "rdl.h"
#include "scheduler.h"

#define MAX_STR_LEN 128

/****************************************************************
 *
 *                 INTERNAL DATA STRUCTURE
 *
 ****************************************************************/

struct stab_struct {
    int i;
    const char *s;
};

/****************************************************************
 *
 *                 STATIC DATA
 *
 ****************************************************************/
static zlist_t *p_queue = NULL;
static zlist_t *r_queue = NULL;
static zlist_t *c_queue = NULL;
static zlist_t *ev_queue = NULL;
static flux_t h = NULL;
static struct rdl *rdl = NULL;
static char* resource = NULL;
static struct rdl *(*find_resources) (flux_t h, struct rdl *rdl, const char *uri,
                                   flux_lwj_t *job, bool *preserve);
static int (*select_resources)(flux_t h, struct rdl *rdl, const char *uri,
                               struct resource *fr, flux_lwj_t *job,
                               bool reserve);
static int (*allocate_resources) (flux_t h, const char *uri, flux_lwj_t *job);
static int (*release_resources) (flux_t h, struct rdl *rdl, const char *uri,
                                 flux_lwj_t *job);

static struct stab_struct jobstate_tab[] = {
    { j_null,      "null" },
    { j_reserved,  "reserved" },
    { j_submitted, "submitted" },
    { j_unsched,   "unsched" },
    { j_pending,   "pending" },
    { j_runrequest,"runrequest" },
    { j_allocated, "allocated" },
    { j_starting,  "starting" },
    { j_running,   "running" },
    { j_cancelled, "cancelled" },
    { j_complete,  "complete" },
    { j_reaped,    "reaped" },
    { -1, NULL },
};

/****************************************************************
 *
 *         Resource Description Library Setup
 *
 ****************************************************************/
static void f_err (flux_t h, const char *msg, ...)
{
    va_list ap;
    va_start (ap, msg);
    flux_vlog (h, LOG_ERR, msg, ap);
    va_end (ap);
}

/* XXX: Borrowed from flux.c and subject to change... */
static void setup_rdl_lua (void)
{
    char *s;
    char  exe_path [MAXPATHLEN];
    char *exe_dir;

    memset (exe_path, 0, MAXPATHLEN);
    if (readlink ("/proc/self/exe", exe_path, MAXPATHLEN - 1) < 0)
        err_exit ("readlink (/proc/self/exe)");
    exe_dir = dirname (exe_path);

    s = getenv ("LUA_CPATH");
    setenvf ("LUA_CPATH", 1, "%s/dlua/?.so;%s", exe_dir, s ? s : ";");
    s = getenv ("LUA_PATH");
    setenvf ("LUA_PATH", 1, "%s/dlua/?.lua;%s", exe_dir, s ? s : ";");

    flux_log (h, LOG_DEBUG, "LUA_PATH %s", getenv ("LUA_PATH"));
    flux_log (h, LOG_DEBUG, "LUA_CPATH %s", getenv ("LUA_CPATH"));

    rdllib_set_default_errf (h, (rdl_err_f)(&f_err));
}

static int
signal_event ( )
{
    int rc = 0;

    if (flux_event_send (h, NULL, "sched.event") < 0) {
        flux_log (h, LOG_ERR,
                 "flux_event_send: %s", strerror (errno));
        rc = -1;
        goto ret;
    }

ret:
    return rc;
}


static flux_lwj_t *
find_lwj (int64_t id)
{
    flux_lwj_t *j = NULL;

    j = zlist_first (p_queue);
    while (j) {
        if (j->lwj_id == id)
            break;
        j = zlist_next (p_queue);
    }
    if (j)
        return j;

    j = zlist_first (r_queue);
    while (j) {
        if (j->lwj_id == id)
            break;
        j = zlist_next (r_queue);
    }

    return j;
}


/****************************************************************
 *
 *              Utility Functions
 *
 ****************************************************************/

static char * ctime_iso8601_now (char *buf, size_t sz)
{
    struct tm tm;
    time_t now = time (NULL);

    memset (buf, 0, sz);

    if (!localtime_r (&now, &tm))
        err_exit ("localtime");
    strftime (buf, sz, "%FT%T", &tm);

    return buf;
}

static int
stab_lookup (struct stab_struct *ss, const char *s)
{
    while (ss->s != NULL) {
        if (!strcmp (ss->s, s))
            return ss->i;
        ss++;
    }
    return -1;
}

static const char *
stab_rlookup (struct stab_struct *ss, int i)
{
    while (ss->s != NULL) {
        if (ss->i == i)
            return ss->s;
        ss++;
    }
    return "unknown";
}

#if 0
static void print_resource (struct resource *r)
{
    json_object *o = rdl_resource_json (r);
    struct resource *c;

    flux_log (h, LOG_DEBUG, "%s", json_object_to_json_string (o));

    rdl_resource_iterator_reset (r);
    while ((c = rdl_resource_next_child (r))) {
        print_resource (c);
        rdl_resource_destroy (c);
    }
    json_object_put (o);
}
#endif

/*
 * Update the job's kvs entry for state and mark the time.
 * Intended to be part of a series of changes, so the caller must
 * invoke the kvs_commit at some future point.
 */
int update_job_state (flux_lwj_t *job, lwj_event_e e)
{
    char buf [64];
    char *key = NULL;
    char *key2 = NULL;
    int rc = -1;
    const char *state;

    ctime_iso8601_now (buf, sizeof (buf));

    state = stab_rlookup (jobstate_tab, e);
    if (!strcmp (state, "unknown")) {
        flux_log (h, LOG_ERR, "unknown job state %d", e);
    } else if (asprintf (&key, "lwj.%ld.state", job->lwj_id) < 0) {
        flux_log (h, LOG_ERR, "update_job_state key create failed");
    } else if (kvs_put_string (h, key, state) < 0) {
        flux_log (h, LOG_ERR, "update_job_state %ld state update failed: %s",
                  job->lwj_id, strerror (errno));
    } else if (asprintf (&key2, "lwj.%ld.%s-time", job->lwj_id, state) < 0) {
        flux_log (h, LOG_ERR, "update_job_state key2 create failed");
    } else if (kvs_put_string (h, key2, buf) < 0) {
        flux_log (h, LOG_ERR, "update_job_state %ld %s-time failed: %s",
                  job->lwj_id, state, strerror (errno));
    } else {
        rc = 0;
        flux_log (h, LOG_DEBUG, "updating job %ld state to %s",
                  job->lwj_id, state);
    }

    free (key);
    free (key2);

    return rc;
}

static inline void
set_event (flux_event_t *e,
           event_class_e c, int ei, flux_lwj_t *j)
{
    e->t = c;
    e->lwj = j;
    switch (c) {
    case lwj_event:
        e->ev.je = (lwj_event_e) ei;
        break;
    case res_event:
        e->ev.re = (res_event_e) ei;
        break;
    default:
        flux_log (h, LOG_ERR, "unknown ev class");
        break;
    }
    return;
}


static int
extract_lwjid (const char *k, int64_t *i)
{
    int rc = 0;
    char *kcopy = NULL;
    char *lwj = NULL;
    char *id = NULL;

    if (!k) {
        rc = -1;
        goto ret;
    }

    kcopy = strdup (k);
    lwj = strtok (kcopy, ".");
    if (strncmp(lwj, "lwj", 3) != 0) {
        rc = -1;
        goto ret;
    }
    id = strtok (NULL, ".");
    *i = strtoul(id, (char **) NULL, 10);

ret:
    free (kcopy);

    return rc;
}

static int
extract_lwjinfo (flux_lwj_t *j)
{
    char *key = NULL;
    char *state;
    int64_t reqnodes = 0;
    int64_t reqtasks = 0;
    int rc = -1;

    j->req = (flux_res_t *) xzmalloc (sizeof (flux_res_t));

    if (asprintf (&key, "lwj.%ld.state", j->lwj_id) < 0) {
        flux_log (h, LOG_ERR, "extract_lwjinfo state key create failed");
        goto ret;
    } else if (kvs_get_string (h, key, &state) < 0) {
        flux_log (h, LOG_ERR, "extract_lwjinfo %s: %s", key, strerror (errno));
        goto ret;
    } else {
        j->state = stab_lookup (jobstate_tab, state);
        flux_log (h, LOG_DEBUG, "extract_lwjinfo got %s: %s", key, state);
        free(key);
        free(state);
    }

    if (asprintf (&key, "lwj.%ld.nnodes", j->lwj_id) < 0) {
        flux_log (h, LOG_ERR, "extract_lwjinfo nnodes key create failed");
        goto ret;
    } else if (kvs_get_int64 (h, key, &reqnodes) < 0) {
        flux_log (h, LOG_ERR, "extract_lwjinfo get %s: %s",
                  key, strerror (errno));
        goto ret;
    } else {
        j->req->nnodes = reqnodes;
        flux_log (h, LOG_DEBUG, "extract_lwjinfo got %s: %ld", key, reqnodes);
        free(key);
    }

    if (asprintf (&key, "lwj.%ld.ntasks", j->lwj_id) < 0) {
        flux_log (h, LOG_ERR, "extract_lwjinfo ntasks key create failed");
        goto ret;
    } else if (kvs_get_int64 (h, key, &reqtasks) < 0) {
        flux_log (h, LOG_ERR, "extract_lwjinfo get %s: %s",
                  key, strerror (errno));
        goto ret;
    } else {
        /* Assuming a 1:1 relationship right now between cores and tasks */
        j->req->ncores = reqtasks;
        flux_log (h, LOG_DEBUG, "extract_lwjinfo got %s: %ld", key, reqtasks);
        free(key);
        j->rdl = NULL;
        j->reserve = false;    /* for now */
        rc = 0;
    }

ret:
    return rc;
}


static void
issue_lwj_event (lwj_event_e e, flux_lwj_t *j)
{
    flux_event_t *ev
        = (flux_event_t *) xzmalloc (sizeof (flux_event_t));
    ev->t = lwj_event;
    ev->ev.je = e;
    ev->lwj = j;

    if (zlist_append (ev_queue, ev) == -1) {
        flux_log (h, LOG_ERR,
                  "enqueuing an event failed");
        goto ret;
    }
    if (signal_event () == -1) {
        flux_log (h, LOG_ERR,
                  "signaling an event failed");
        goto ret;
    }

ret:
    return;
}

/****************************************************************
 *
 *         Scheduler Activities
 *
 ****************************************************************/


/*
 * Update the resource and job records to reflect the allocation.
 */
static int
update_records (flux_lwj_t *job)
{
    int rc = -1;

    if (update_job_state (job, j_allocated)) {
        flux_log (h, LOG_ERR, "update_job failed to update job %ld to %s",
                  job->lwj_id, stab_rlookup (jobstate_tab, j_allocated));
    } else if ((*allocate_resources) (h, resource, job)) {
        flux_log (h, LOG_ERR, "failed to allocate resources for job %ld",
                  job->lwj_id);
    } else if (kvs_commit (h) < 0) {
        flux_log (h, LOG_ERR, "kvs_commit error!");
    } else {
        rc = 0;
    }

    return rc;
}

/*
 * schedule_job() searches through all of the idle resources to
 * satisfy a job's requirements.  If enough resources are found, it
 * proceeds to allocate those resources and update the kvs's lwj entry
 * in preparation for job execution.
 */
int schedule_job (struct rdl *rdl, const char *uri, flux_lwj_t *job)
{
    bool reserve = false;
    int rc = -1;
    struct rdl_accumulator *a = NULL;
    struct resource *fr = NULL;         /* found resource */
    struct rdl *frdl = NULL;            /* found rdl */

    if ((frdl = (*find_resources) (h, rdl, uri, job, &reserve))) {
        if ((fr = rdl_resource_get (frdl, uri))) {
            a = rdl_accumulator_create (rdl);
            if (!select_resources (h, rdl, uri, fr, job, reserve)) {
                /* Transition the job back to submitted to prevent the
                 * scheduler from trying to schedule it again */
                job->state = j_submitted;
                rc = update_records (job);
            }
        } else {
            flux_log (h, LOG_ERR, "failed to get found resource for job %ld",
                      job->lwj_id);
        }
        rdl_destroy (frdl);
    }

    return rc;
}

int schedule_jobs (struct rdl *rdl, const char *uri, zlist_t *jobs)
{
    flux_lwj_t *job = NULL;
    int rc = 0;

    job = zlist_first (jobs);
    while (!rc && job) {
        if (job->state == j_unsched) {
            rc = schedule_job (rdl, uri, job);
        }
        job = zlist_next (jobs);
    }

    return rc;
}


/****************************************************************
 *
 *         Actions Led by Current State + an Event
 *
 ****************************************************************/

static int
request_run (flux_lwj_t *job)
{
    int rc = -1;

    if (update_job_state (job, j_runrequest) < 0) {
        flux_log (h, LOG_ERR, "request_run failed to update job %ld to %s",
                  job->lwj_id, stab_rlookup (jobstate_tab, j_runrequest));
    } else if (kvs_commit (h) < 0) {
        flux_log (h, LOG_ERR, "kvs_commit error!");
    } else if (flux_event_send (h, NULL, "rexec.run.%ld", job->lwj_id) < 0) {
        flux_log (h, LOG_ERR, "request_run event send failed: %s",
                  strerror (errno));
    } else {
        flux_log (h, LOG_DEBUG, "job %ld runrequest", job->lwj_id);
        rc = 0;
    }

    return rc;
}


static int
issue_res_event (flux_lwj_t *lwj)
{
    int rc = 0;
    flux_event_t *newev
        = (flux_event_t *) xzmalloc (sizeof (flux_event_t));

    // TODO: how to update the status of each entry as "free"
    // then destroy zlist_t without having to destroy
    // the elements
    // release lwj->resource

    newev->t = res_event;
    newev->ev.re = r_released;
    newev->lwj = lwj;

    if (zlist_append (ev_queue, newev) == -1) {
        flux_log (h, LOG_ERR,
                  "enqueuing an event failed");
        rc = -1;
        goto ret;
    }
    if (signal_event () == -1) {
        flux_log (h, LOG_ERR,
                  "signal the event-enqueued event ");
        rc = -1;
        goto ret;
    }

ret:
    return rc;
}

static int
move_to_r_queue (flux_lwj_t *lwj)
{
    zlist_remove (p_queue, lwj);
    return zlist_append (r_queue, lwj);
}

static int
move_to_c_queue (flux_lwj_t *lwj)
{
    zlist_remove (r_queue, lwj);
    return zlist_append (c_queue, lwj);
}


static int
action_j_event (flux_event_t *e)
{
    /* e->lwj->state is the current state
     * e->ev.je      is the new state
     */
    flux_log (h, LOG_DEBUG, "attempting job %ld state change from %s to %s",
              e->lwj->lwj_id, stab_rlookup (jobstate_tab, e->lwj->state),
                              stab_rlookup (jobstate_tab, e->ev.je));

    switch (e->lwj->state) {
    case j_null:
        if (e->ev.je == j_reserved) {
            e->lwj->state = j_reserved;
            break;
        }
        /* deliberate fall-through */
    case j_reserved:
        if (e->ev.je != j_submitted) {
            goto bad_transition;
        }
        extract_lwjinfo (e->lwj);
        if (e->lwj->state != j_submitted) {
            flux_log (h, LOG_ERR,
                      "job %ld read state mismatch ", e->lwj->lwj_id);
            goto bad_transition;
        }
        /* Transition the job temporarily to unscheduled to flag it as
         * a candidate to be scheduled */
        flux_log (h, LOG_DEBUG, "setting %ld to unscheduled state",
                  e->lwj->lwj_id);
        e->lwj->state = j_unsched;
        schedule_jobs (rdl, resource, p_queue);
        break;

    case j_submitted:
        if (e->ev.je != j_allocated) {
            goto bad_transition;
        }
        e->lwj->state = j_allocated;
        request_run(e->lwj);
        break;

    case j_unsched:
        /* TODO */
        goto bad_transition;
        break;

    case j_pending:
        /* TODO */
        goto bad_transition;
        break;

    case j_allocated:
        if (e->ev.je != j_runrequest) {
            goto bad_transition;
        }
        e->lwj->state = j_runrequest;
        break;

    case j_runrequest:
        if (e->ev.je != j_starting) {
            goto bad_transition;
        }
        e->lwj->state = j_starting;
        break;

    case j_starting:
        if (e->ev.je != j_running) {
            goto bad_transition;
        }
        e->lwj->state = j_running;
        move_to_r_queue (e->lwj);
        break;

    case j_running:
        if (e->ev.je != j_complete) {
            goto bad_transition;
        }
        /* TODO move this to j_complete case once reaped is implemented */
        move_to_c_queue (e->lwj);
        issue_res_event (e->lwj);
        break;

    case j_cancelled:
        /* TODO */
        goto bad_transition;
        break;

    case j_complete:
        if (e->ev.je != j_reaped) {
            goto bad_transition;
        }
//        move_to_c_queue (e->lwj);
        break;

    case j_reaped:
        if (e->ev.je != j_complete) {
            goto bad_transition;
        }
        e->lwj->state = j_reaped;
        break;

    default:
        flux_log (h, LOG_ERR, "job %ld unknown state %d",
                  e->lwj->lwj_id, e->lwj->state);
        break;
    }

    return 0;

bad_transition:
    flux_log (h, LOG_ERR, "job %ld bad state transition from %s to %s",
              e->lwj->lwj_id, stab_rlookup (jobstate_tab, e->lwj->state),
                              stab_rlookup (jobstate_tab, e->ev.je));
    return -1;
}


static int
action_r_event (flux_event_t *e)
{
    int rc = -1;

    if ((e->ev.re == r_released) || (e->ev.re == r_attempt)) {
        (*release_resources) (h, rdl, resource, e->lwj);
        schedule_jobs (rdl, resource, p_queue);
        rc = 0;
    }

    return rc;
}


static int
action (flux_event_t *e)
{
    int rc = 0;

    switch (e->t) {
    case lwj_event:
        rc = action_j_event (e);
        break;

    case res_event:
        rc = action_r_event (e);
        break;

    default:
        flux_log (h, LOG_ERR, "unknown event type");
        break;
    }

    return rc;
}


/****************************************************************
 *
 *         Abstractions for KVS Callback Registeration
 *
 ****************************************************************/
static int
wait_for_lwj_init ()
{
    int rc = 0;
    kvsdir_t dir = NULL;

    if (kvs_watch_once_dir (h, &dir, "lwj") < 0) {
        flux_log (h, LOG_ERR, "wait_for_lwj_init: %s",
                  strerror (errno));
        rc = -1;
        goto ret;
    }

    flux_log (h, LOG_DEBUG, "wait_for_lwj_init %s",
              kvsdir_key(dir));

ret:
    if (dir)
        kvsdir_destroy (dir);
    return rc;
}


static int
reg_newlwj_hdlr (KVSSetInt64F *func)
{
    if (kvs_watch_int64 (h,"lwj.next-id", func, (void *) h) < 0) {
        flux_log (h, LOG_ERR, "watch lwj.next-id: %s",
                  strerror (errno));
        return -1;
    }
    flux_log (h, LOG_DEBUG, "registered lwj creation callback");

    return 0;
}


static int
reg_lwj_state_hdlr (const char *path, KVSSetStringF *func)
{
    int rc = 0;
    char *k = NULL;

    asprintf (&k, "%s.state", path);
    if (kvs_watch_string (h, k, func, (void *)h) < 0) {
        flux_log (h, LOG_ERR,
                  "watch a lwj state in %s: %s.",
                  k, strerror (errno));
        rc = -1;
        goto ret;
    }
    flux_log (h, LOG_DEBUG, "registered lwj %s.state change callback", path);

ret:
    free (k);
    return rc;
}


/****************************************************************
 *                KVS Watch Callback Functions
 ****************************************************************/
static void
lwjstate_cb (const char *key, const char *val, void *arg, int errnum)
{
    int64_t lwj_id;
    flux_lwj_t *j = NULL;
    lwj_event_e e;

    if (errnum > 0) {
        /* Ignore ENOENT.  It is expected when this cb is called right
         * after registration.
         */
        if (errnum != ENOENT) {
            flux_log (h, LOG_ERR, "lwjstate_cb key(%s), val(%s): %s",
                      key, val, strerror (errnum));
        }
        goto ret;
    }

    if (extract_lwjid (key, &lwj_id) == -1) {
        flux_log (h, LOG_ERR, "ill-formed key");
        goto ret;
    }
    flux_log (h, LOG_DEBUG, "lwjstate_cb: %ld, %s", lwj_id, val);

    j = find_lwj (lwj_id);
    if (j) {
        e = stab_lookup (jobstate_tab, val);
        issue_lwj_event (e, j);
    } else
        flux_log (h, LOG_ERR, "lwjstate_cb: find_lwj %ld failed", lwj_id);

ret:
    return;
}

/* The val argument is for the *next* job id.  Hence, the job id of
 * the new job will be (val - 1).
 */
static void
newlwj_cb (const char *key, int64_t val, void *arg, int errnum)
{
    char path[MAX_STR_LEN];
    flux_lwj_t *j = NULL;

    if (errnum > 0) {
        /* Ignore ENOENT.  It is expected when this cb is called right
         * after registration.
         */
        if (errnum != ENOENT) {
            flux_log (h, LOG_ERR, "newlwj_cb key(%s), val(%ld): %s",
                      key, val, strerror (errnum));
            goto error;
        }
        goto ret;
    } else if (val < 0) {
        flux_log (h, LOG_ERR, "newlwj_cb key(%s), val(%ld)", key, val);
        goto error;
    } else {
        flux_log (h, LOG_DEBUG, "newlwj_cb key(%s), val(%ld)", key, val);
    }

    if ( !(j = (flux_lwj_t *) xzmalloc (sizeof (flux_lwj_t))) ) {
        flux_log (h, LOG_ERR, "oom");
        goto error;
    }
    j->lwj_id = val - 1;
    j->state = j_null;
    snprintf (path, MAX_STR_LEN, "lwj.%ld", j->lwj_id);
    if (zlist_append (p_queue, j) == -1) {
        flux_log (h, LOG_ERR,
                  "appending a job to pending queue failed");
        goto error;
    }
    if (reg_lwj_state_hdlr (path, (KVSSetStringF *) lwjstate_cb) == -1) {
        flux_log (h, LOG_ERR,
                  "register lwj state change "
                  "handling callback: %s",
                  strerror (errno));
        goto error;
    }
ret:
    return;

error:
    if (j)
        free (j);

    return;
}


static int
event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    flux_event_t *e = NULL;

    while ( (e = zlist_pop (ev_queue)) != NULL) {
        action (e);
        free (e);
    }

    zmsg_destroy (zmsg);

    return 0;
}


/****************************************************************
 *
 *        High Level Job and Resource Event Handlers
 *
 ****************************************************************/
int mod_main (flux_t p, zhash_t *args)
{
    int rc = 0;
    char *path;
    struct rdllib *l = NULL;
    struct resource *r = NULL;
    void *dso;

    h = p;
    if (flux_rank (h) != 0) {
        flux_log (h, LOG_ERR, "sched module must only run on rank 0");
        rc = -1;
        goto ret;
    }
    flux_log (h, LOG_INFO, "sched comms module starting");

    if (!(dso = dlopen ("plugins/schedplugin1.so", RTLD_NOW | RTLD_LOCAL))) {
        flux_log (h, LOG_ERR, "failed to open sched plugin: %s",
                  dlerror ());
        rc = -1;
        goto ret;
    }
    if (!(find_resources = dlsym (dso, "find_resources")) || !*find_resources) {
        flux_log (h, LOG_ERR, "failed to load find_resources symbol: %s",
                  dlerror ());
        rc = -1;
        goto ret;
    }
    if (!(select_resources = dlsym (dso, "select_resources")) ||
        !*select_resources) {
        flux_log (h, LOG_ERR, "failed to load select_resources symbol: %s",
                  dlerror ());
        rc = -1;
        goto ret;
    }
    if (!(allocate_resources = dlsym (dso, "allocate_resources")) ||
        !*allocate_resources) {
        flux_log (h, LOG_ERR, "failed to load allocate_resources symbol: %s",
                  dlerror ());
        rc = -1;
        goto ret;
    }
    if (!(release_resources = dlsym (dso, "release_resources")) ||
        !*release_resources) {
        flux_log (h, LOG_ERR, "failed to load release_resources symbol: %s",
                  dlerror ());
        rc = -1;
        goto ret;
    }

    if (!(path = zhash_lookup (args, "rdl-conf"))) {
        flux_log (h, LOG_ERR, "rdl-conf argument is not set");
        rc = -1;
        goto ret;
    }
    setup_rdl_lua ();
    if (!(l = rdllib_open ()) || !(rdl = rdl_loadfile (l, path))) {
        flux_log (h, LOG_ERR, "failed to load resources from %s: %s",
                  path, strerror (errno));
        rc = -1;
        goto ret;
    }
    if (!(resource = zhash_lookup (args, "rdl-resource"))) {
        flux_log (h, LOG_INFO, "using default rdl resource");
        resource = "default";
    }

    if (!(r = rdl_resource_get (rdl, resource))) {
        flux_log (h, LOG_ERR, "failed to get %s: %s", resource,
                  strerror (errno));
        rc = -1;
        goto ret;
    }

    p_queue = zlist_new ();
    r_queue = zlist_new ();
    c_queue = zlist_new ();
    ev_queue = zlist_new ();
    if (!p_queue || !r_queue || !c_queue || !ev_queue) {
        flux_log (h, LOG_ERR,
                  "init for queues failed: %s",
                  strerror (errno));
        rc = -1;
        goto ret;
    }
    if (flux_event_subscribe (h, "sched.event") < 0) {
        flux_log (h, LOG_ERR,
                  "subscribing to event: %s",
                  strerror (errno));
        rc = -1;
        goto ret;
    }
    if (flux_msghandler_add (h, FLUX_MSGTYPE_EVENT, "sched.event",
                             event_cb, NULL) < 0) {
        flux_log (h, LOG_ERR,
                  "register event handling callback: %s",
                  strerror (errno));
        rc = -1;
        goto ret;
    }
    if (wait_for_lwj_init () == -1) {
        flux_log (h, LOG_ERR, "wait for lwj failed: %s",
                  strerror (errno));
        rc = -1;
        goto ret;
    }
    if (reg_newlwj_hdlr ((KVSSetInt64F*) newlwj_cb) == -1) {
        flux_log (h, LOG_ERR,
                  "register new lwj handling "
                  "callback: %s",
                  strerror (errno));
        rc = -1;
        goto ret;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR,
                  "flux_reactor_start: %s",
                  strerror (errno));
        rc =  -1;
        goto ret;
    }

    zlist_destroy (&p_queue);
    zlist_destroy (&r_queue);
    zlist_destroy (&c_queue);
    zlist_destroy (&ev_queue);

    rdllib_close(l);
    dlclose (dso);

ret:
    return rc;
}

MOD_NAME ("sched");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
