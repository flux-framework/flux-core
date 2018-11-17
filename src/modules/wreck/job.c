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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>

#include <zmq.h>
#include <czmq.h>
#include <inttypes.h>
#include <stdbool.h>
#if WITH_TCMALLOC
#if HAVE_GPERFTOOLS_HEAP_PROFILER_H
  #include <gperftools/heap-profiler.h>
#elif HAVE_GOOGLE_HEAP_PROFILER_H
  #include <google/heap-profiler.h>
#else
  #error gperftools headers not configured
#endif
#endif /* WITH_TCMALLOC */

#include <jansson.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libsubprocess/subprocess.h"

#include "rcalc.h"
#include "wreck_job.h"

#define MAX_JOB_PATH    1024

/*
 *  lwj directory hierarchy parameters:
 *   XXX: should be in module context struct perhaps, but
 *    this all needs to go away soon anyway, so...
 *
 *  directory levels is the number of parent directories
 *   (e.g. 3 would result in lwj-active.x.y.z.jobid,
 *    0 is lwj.jobid)
 *
 *  bits_per_directory is the number of prefix bits to use
 *   for each parent directory, results in 2^bits entries
 *   per subdirectory, except for the top-level which will
 *   grow without bound (well up to 64bit lwj id values).
 *
 *  These values can be set as broker attrs during flux-start,
 *   e.g. flux start -o,-Swreck.lwj-dir-levels=3
 *                   -o,-Swreck.lwj-bits-per-dir=8
 */
static int kvs_dir_levels = 2;
static int kvs_bits_per_dir = 7;

uint32_t broker_rank;
const char *local_uri = NULL;

zhash_t *active_jobs = NULL;

/*
 *  Return as 64bit integer the portion of integer `n`
 *   masked from bit position `a` to position `b`,
 *   then subsequently shifted by `a` bits (to keep
 *   numbers small).
 */
static inline uint64_t prefix64 (uint64_t n, int a, int b)
{
    uint64_t mask = ((-1ULL >> (64 - b)) & ~((1ULL << a) - 1));
    return ((n & mask) >> a);
}

/*
 *  Convert lwj id to kvs path under `lwj-active` using a kind of
 *   prefix hiearchy of max levels `levels`, using `bits_per_dir` bits
 *   for each directory. Returns a kvs key path or NULL on failure.
 */
static char * lwj_to_path (uint64_t id, int levels, int bits_per_dir)
{
    char buf [MAX_JOB_PATH] = "lwj";
    int len = 3;
    int nleft = sizeof (buf) - len;
    int i, n;

    /* Build up kvs directory from lwj. down */
    for (i = levels; i > 0; i--) {
        int b = bits_per_dir * i;
        uint64_t d = prefix64 (id, b, b + bits_per_dir);
        if ((n = snprintf (buf+len, nleft, ".%"PRIu64, d)) < 0 || n > nleft)
            return NULL;
        len += n;
        nleft -= n;
    }
    n = snprintf (buf+len, sizeof (buf) - len, ".%"PRIu64, id);
    if (n < 0 || n > nleft)
        return NULL;
    return (strdup (buf));
}

static char * id_to_path (uint64_t id)
{
    return (lwj_to_path (id, kvs_dir_levels, kvs_bits_per_dir));
}

static flux_future_t *next_jobid (flux_t *h)
{
    flux_future_t *f;

    f = flux_rpc_pack (h, "seq.fetch", 0, 0, "{s:s,s:i,s:i,s:b}",
                       "name", "lwj",
                       "preincrement", 1,
                       "postincrement", 0,
                       "create", true);
    return f;
}

static int next_jobid_get (flux_future_t *f, int64_t *id)
{
    int64_t jobid;
    if (flux_rpc_get_unpack (f, "{s:I}", "value", &jobid) < 0)
        return -1;
    *id = jobid;
    return 0;
}

static char * realtime_string (char *buf, size_t sz)
{
    struct timespec tm;
    clock_gettime (CLOCK_REALTIME, &tm);
    memset (buf, 0, sz);
    snprintf (buf, sz, "%ju.%06ld", (uintmax_t) tm.tv_sec, tm.tv_nsec/1000);
    return (buf);
}

/* Publish wreck.state.<state> event.
 * Future is fulfilled once event has received a sequence number.
 * This ensures that response to job create request is not sent until this
 * happens.  See issue #337.
 */
static flux_future_t *send_create_event (flux_t *h, struct wreck_job *job)
{
    char topic[64];
    flux_future_t *f;
    int flags = 0;

    if (snprintf (topic, sizeof (topic), "wreck.state.%s", job->state)
                                                        >= sizeof (topic)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_event_publish_pack (h, topic, flags,
                             "{s:I,s:s,s:i,s:i,s:i,s:i,s:i}",
                             "jobid", job->id,
                             "kvs_path", job->kvs_path,
                             "ntasks", job->ntasks,
                             "ncores", job->ncores,
                             "nnodes", job->nnodes,
                             "ngpus", job->ngpus,
                             "walltime", job->walltime)))
        return NULL;
    return f;
}

static int add_jobinfo_txn (flux_kvs_txn_t *txn, struct wreck_job *job)
{
    char buf [64];
    const char *json_str;
    char key[MAX_JOB_PATH];
    size_t keysz = sizeof (key);
    flux_msg_t *msg = wreck_job_get_aux (job);

    const char *k;
    json_t *v;
    json_t *o = NULL;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!json_str || !(o = json_loads (json_str, 0, NULL)))
        goto inval;
    if (snprintf (key, sizeof (key), "%s.state", job->kvs_path) >= sizeof (key))
        goto inval;
    if (flux_kvs_txn_pack (txn, 0, key, "s", job->state) < 0)
        goto error;

    json_object_foreach (o, k, v) {
        int rc;
        char *s;
        if (snprintf (key, keysz, "%s.%s", job->kvs_path, k) >= keysz)
            goto inval;
        if (!(s = json_dumps (v, JSON_COMPACT|JSON_ENCODE_ANY)))
            goto error;
        rc = flux_kvs_txn_put (txn, 0, key, s);
        free (s);
        if (rc < 0)
            goto error;
    }
    if (snprintf (key, sizeof (key), "%s.create-time", job->kvs_path)
                                                        >= sizeof (key))
        goto inval;
    if (flux_kvs_txn_pack (txn, 0, key, "s",
                           realtime_string (buf, sizeof (buf))) < 0)
        goto error;
    json_decref (o);
    return 0;
inval:
    errno = EINVAL;
error:
    json_decref (o);
    return -1;
}

static bool ping_sched (flux_t *h)
{
    bool retval = false;
    flux_future_t *f;
    if (!(f = flux_rpc_pack (h, "sched.ping", 0, 0, "{s:i}", "seq", 0))) {
        flux_log_error (h, "ping_sched");
        goto out;
    }
    if (flux_future_get (f, NULL) >= 0)
        retval = true;
out:
    flux_future_destroy (f);
    return (retval);
}

static bool sched_loaded (flux_t *h)
{
    static bool v = false;
    if (!v && ping_sched (h))
        v = true;
    return (v);
}

/*
 *  Respond to job.submit-nocreate rpc, which allows a job previously
 *   created by "job.create" to be submitted (The "job.submit" rpc
 *   creates *and* submits a job). Set the job state to "submitted"
 *   and issue the wreck.state.submitted event with appropriate payload.
 *
 *  This rpc is used by flux-wreckrun when a scheduler is loaded so that
 *   "interactive" jobs are subject to normal scheduling.
 *   (The -I, --immediate flag of flux-wreckrun can be used to disable
 *   this behavior)
 */
static void job_submit_only (flux_t *h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    int64_t jobid;
    struct wreck_job *job = NULL;
    flux_future_t *f = NULL;;

    if (!sched_loaded (h)) {
        errno = ENOSYS;
        goto error;
    }
    if (flux_request_unpack (msg, NULL, "{s:I}", "jobid", &jobid) < 0)
        goto error;

    if (!(job = wreck_job_lookup (jobid, active_jobs))) {
        errno = ENOENT;
        goto error;
    }
    wreck_job_set_state (job, "submitted");
    if (!(f = send_create_event (h, job)))
        goto error;
    if (flux_future_get (f, NULL) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{s:I}", "jobid", job->id) < 0)
        flux_log_error (h, "flux_respond");
    flux_future_destroy (f);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "flux_respond");
    flux_future_destroy (f);
}

/* Handle request to broadcast wreck.state.<state> event.
 * This concludes the continuation chain started at job_create_cb().
 * Respond to the original request and destroy 'job'.
 */
static void job_create_event_continuation (flux_future_t *f, void *arg)
{
    struct wreck_job *job = arg;
    flux_t *h = flux_future_get_flux (f);
    flux_msg_t *msg = wreck_job_get_aux (job);

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    else {
        if (flux_respond_pack (h, msg, "{s:I,s:s,s:s}",
                                       "jobid", job->id,
                                       "state", job->state,
                                       "kvs_path", job->kvs_path) < 0)
            flux_log_error (h, "flux_respond_pack");
    }
    flux_future_destroy (f);
}


/* Handle KVS commit response, then send request to broadcast
 * wreck.state.<state> event.
 * Function is continued in job_create_event_continuation().
 */
static void job_create_kvs_continuation (flux_future_t *f, void *arg)
{
    struct wreck_job *job = arg;
    flux_t *h = flux_future_get_flux (f);
    flux_msg_t *msg = wreck_job_get_aux (job);
    flux_future_t *f_next = NULL;

    if (flux_future_get (f, NULL) < 0)
        goto error;

    /* Preemptively insert this job into the active job hash on this
     *  node, making it available for use by job_submit_only (). We do
     *  this *before* we send the event so we avoid racing with the
     *  event handler that also inserts active jobs.
     */
    if (wreck_job_insert (job, active_jobs) < 0)
        goto error;
    if (!(f_next = send_create_event (h, job)))
        goto error;
    if (flux_future_then (f_next, -1., job_create_event_continuation, job) < 0)
        goto error;
    flux_future_destroy (f);
    return;
error:
    flux_log_error (h, "%s", __FUNCTION__);
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_future_destroy (f_next);
    flux_future_destroy (f);
}

/* Handle next available jobid response, then issue KVS commit request
 * to write job data to KVS.
 * Function is continued in job_create_kvs_continuation().
 */
static void job_create_continuation (flux_future_t *f, void *arg)
{
    struct wreck_job *job = arg;
    flux_t *h = flux_future_get_flux (f);
    flux_msg_t *msg = wreck_job_get_aux (job);
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f_next = NULL;

    if (next_jobid_get (f, &job->id) < 0)
        goto error;
    if (!(job->kvs_path = id_to_path (job->id)))
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (add_jobinfo_txn (txn, job) < 0)
        goto error;
    if (!(f_next = flux_kvs_commit (h, 0, txn)))
        goto error;
    if (flux_future_then (f_next, -1., job_create_kvs_continuation, job) < 0)
        goto error;
    flux_log (h, LOG_DEBUG, "Setting job %lld to %s", (long long)job->id,
                                                                 job->state);
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    return;
error:
    flux_log_error (h, "%s", __FUNCTION__);
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f_next);
    flux_future_destroy (f);
    wreck_job_destroy (job);
}

/* Handle job.create and job.submit requests.
 * Create 'job', then send request for next available jobid.
 * Function is continued in job_create_continuation().
 */
static void job_create_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    const char *topic;
    flux_msg_t *cpy;
    struct wreck_job *job;
    flux_future_t *f = NULL;

    if (!(job = wreck_job_create ()))
        goto error;
    if (flux_request_unpack (msg, &topic, "{s?:i s?:i s?:i s?:i s?:i}",
                                          "ntasks", &job->ntasks,
                                          "nnodes", &job->nnodes,
                                          "ncores", &job->ncores,
                                          "ngpus", &job->ngpus,
                                          "walltime", &job->walltime) < 0)
        goto error;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto error;
    wreck_job_set_aux (job, cpy, (flux_free_f)flux_msg_destroy);
    if (strcmp (topic, "job.create") == 0)
        wreck_job_set_state (job, "reserved");
    else if (strcmp (topic, "job.submit") == 0) {
        if (!sched_loaded (h)) {
            errno = ENOSYS;
            goto error;
        }
        wreck_job_set_state (job, "submitted");
    }
    if (!(f = next_jobid (h)))
        goto error;
    if (flux_future_then (f, -1., job_create_continuation, job) < 0)
        goto error;
    return;
error:
    flux_log_error (h, "%s", __FUNCTION__);
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    wreck_job_destroy (job);
    flux_future_destroy (f);
}

static json_t *json_id_to_json_path (flux_t *h, json_t *value)
{
    int64_t id;
    char *path = NULL;
    json_t *o = NULL;
    int errnum = EPROTO;

    if (!json_is_integer (value) || (id = json_integer_value (value)) < 0)
        goto out;
    if (!(path = id_to_path (id))) {
        errnum = errno;
        flux_log (h, LOG_ERR, "kvspath_cb: lwj_to_path failed");
        goto out;
    }
    if (!(o = json_string (path))) {
        errnum = errno;
        flux_log_error (h, "kvspath_cb: json_string");
        goto out;
    }
    errnum = 0;
out:
    free (path);
    errno = errnum;
    return (o);
}

static void job_kvspath_cb (flux_t *h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    int errnum = EPROTO;
    size_t index;
    json_t *id_list = NULL;
    json_t *paths = NULL;
    json_t *value;

    if ((flux_request_unpack (msg, NULL, "{s:o}", "ids", &id_list) < 0)
        || !json_is_array (id_list)) {
        flux_log_error (h, "kvspath_cb failed to unpack message");
        goto out;
    }

    paths = json_array ();
    json_array_foreach (id_list, index, value) {
        json_t *o = json_id_to_json_path (h, value);
        if (o == NULL) {
            errnum = errno;
            goto out;
        }
        if (json_array_append_new (paths, o) < 0) {
            errnum = errno;
            flux_log_error (h, "kvspath_cb: json_array_append_new");
            json_decref (o);
            goto out;
        }
    }
    if (flux_respond_pack (h, msg, "{s:O}", "paths", paths) < 0)
        flux_log_error (h, "kvspath_cb: flux_respond_pack");
    errnum = 0;
out:
    if (errnum && flux_respond (h, msg, errnum, NULL) < 0)
        flux_log_error (h, "kvspath_cb: flux_respond");
    json_decref (paths);
}

static int flux_attr_set_int (flux_t *h, const char *attr, int val)
{
    char buf [16];
    int n = snprintf (buf, sizeof (buf), "%d", val);
    if (n < 0 || n >= sizeof (buf))
        return (-1);
    return flux_attr_set (h, attr, buf);
}

static int flux_attr_get_int (flux_t *h, const char *attr, int *valp)
{
    long n;
    const char *tmp;
    char *p;

    if ((tmp = flux_attr_get (h, attr)) == NULL)
        return (-1);
    n = strtoul (tmp, &p, 10);
    if (n == LONG_MAX)
        return (-1);
    if ((p == tmp) || (*p != '\0')) {
        errno = EINVAL;
        return (-1);
    }
    *valp = (int) n;
    return (0);
}

static void completion_cb (flux_subprocess_t *p)
{
    flux_t *h;
    struct wreck_job *job = NULL;
    int tmp;

    h = flux_subprocess_aux_get (p, "handle");
    job = flux_subprocess_aux_get (p, "job");

    assert (h && job);

    /* skip output text if exit code == 0 */
    if (!(tmp = flux_subprocess_exit_code (p)))
        goto cleanup;

    if (tmp > 0) {
        flux_log_error (h, "job%ju: wrexecd: Exit %d",
                        (uintmax_t) job->id, tmp);
    }
    else if (tmp < 0) {
        if ((tmp = flux_subprocess_signaled (p)) < 0)
            flux_log_error (h, "job%ju: unknown exit status", (uintmax_t) job->id);
        else
            flux_log_error (h, "job%ju: wrexecd: %s",
                            (uintmax_t) job->id, strsignal (tmp));

    }

cleanup:
    wreck_job_destroy (job);
    flux_subprocess_destroy (p);
}

static void state_change_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    flux_t *h;
    struct wreck_job *job;

    h = flux_subprocess_aux_get (p, "handle");
    job = flux_subprocess_aux_get (p, "job");

    assert (h && job);

    // XXX: Update job state to failed
    if (state == FLUX_SUBPROCESS_EXEC_FAILED) {
        flux_log_error (h, "spawn: job%ju: wrexecd exec failure", (uintmax_t) job->id);
        flux_subprocess_destroy (p);
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        flux_log_error (h, "spawn: job%ju: wrexecd failure", (uintmax_t) job->id);
        flux_subprocess_destroy (p);
    }
}

static void io_cb (flux_subprocess_t *p, const char *stream)
{
    int lenp = 0;
    const char *ptr;

    if ((ptr = flux_subprocess_read_line (p, stream, &lenp))
        && lenp > 0) {
        flux_t *h;
        struct wreck_job *job;
        int level = LOG_INFO;

        h = flux_subprocess_aux_get (p, "handle");
        job = flux_subprocess_aux_get (p, "job");

        assert (h && job);

        if (!strcasecmp (stream, "STDERR"))
            level = LOG_ERR;

        flux_log (h, level,
                  "job%ju: wrexecd says: %s",
                  (uintmax_t) job->id, ptr);
    }
}

static flux_cmd_t *wrexecd_cmd_create (flux_t *h, struct wreck_job *job)
{
    flux_cmd_t *cmd = NULL;
    const char *wrexecd_path;
    char *cwd = NULL;
    char buf [4096];
    int n;

    if (!(cmd = flux_cmd_create (0, NULL, NULL))) {
        flux_log_error (h, "wrexecd_cmd_create: flux_cmd_create");
        goto error;
    }
    if (!(wrexecd_path = flux_attr_get (h, "wrexec.wrexecd_path"))) {
        flux_log_error (h, "wrexecd_cmd_create: flux_attr_get");
        goto error;
    }
    if (flux_cmd_argv_append (cmd, "%s", wrexecd_path) < 0) {
        flux_log_error (h, "wrexecd_cmd_create: flux_cmd_argv_append");
        goto error;
    }
    n = snprintf (buf, sizeof(buf), "--lwj-id=%ju", (uintmax_t) job->id);
    if ((n >= sizeof (buf)) || (n < 0)) {
        flux_log_error (h, "failed to append id to cmdline for job%ju\n",
                            (uintmax_t) job->id);
        goto error;
    }
    if (flux_cmd_argv_append (cmd, "%s", buf) < 0) {
        flux_log_error (h, "wrexecd_cmd_create: flux_cmd_argv_append");
        goto error;
    }
    n = snprintf (buf, sizeof (buf), "--kvs-path=%s", job->kvs_path);
    if ((n >= sizeof (buf)) || (n < 0)) {
        flux_log_error (h, "failed to append kvspath to cmdline for job%ju\n",
                            (uintmax_t) job->id);
        goto error;
    }
    if (flux_cmd_argv_append (cmd, "%s", buf) < 0) {
        flux_log_error (h, "wrexecd_cmd_create: flux_cmd_argv_append");
        goto error;
    }
    /* flux_rexec() requires cwd to be set */
    if (!(cwd = get_current_dir_name ())) {
        flux_log_error (h, "wrexecd_cmd_create: get_current_dir_name");
        goto error;
    }
    if (flux_cmd_setcwd (cmd, cwd) < 0) {
        flux_log_error (h, "wrexecd_cmd_create: flux_cmd_setcwd");
        goto error;
    }

    free (cwd);
    return (cmd);
error:
    free (cwd);
    flux_cmd_destroy (cmd);
    return (NULL);
}

static int spawn_exec_handler (flux_t *h, struct wreck_job *job)
{
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_change_cb,
        .on_channel_out = NULL,
        .on_stdout = io_cb,
        .on_stderr = io_cb
    };

    if (!(cmd = wrexecd_cmd_create (h, job))) {
        flux_log_error (h, "wrexecd_cmd_create");
        goto error;
    }

    if (!(p = flux_rexec (h, FLUX_NODEID_ANY, 0, cmd, &ops))) {
        flux_log_error (h, "flux_rexec");
        goto error;
    }

    if (flux_subprocess_aux_set (p, "handle", h, NULL) < 0) {
        flux_log_error (h, "flux_subprocess_aux_set");
        goto error;
    }

    if (flux_subprocess_aux_set (p, "job", job, NULL) < 0) {
        flux_log_error (h, "flux_subprocess_aux_set");
        goto error;
    }

    /*  Take a reference on this job since it is now embedded in
     *   a flux rexec.
     */
    flux_cmd_destroy (cmd);
    wreck_job_incref (job);
    return (0);

error:
    flux_cmd_destroy (cmd);
    flux_subprocess_destroy (p);
    return (-1);
}

static bool Rlite_targets_this_node (flux_t *h, const char *key,
                                     const char *R_lite)
{
    rcalc_t *r = NULL;
    bool result;

    if (!(r = rcalc_create (R_lite))) {
        if (broker_rank == 0)
            flux_log (h, LOG_ERR, "Unable to parse %s", key);
        return false;
    }
    result = rcalc_has_rank (r, broker_rank);
    rcalc_destroy (r);
    return result;
}

/* Handle response to lookup of R_lite.  If this node is targetted,
 * spawn wrexecd.  If R_lite doesn't exist, fallback to old method
 * of looking up rank.N, with one more continuation.
 */
static void runevent_continuation (flux_future_t *f, void *arg)
{
    struct wreck_job *job = arg;
    flux_t *h = flux_future_get_flux (f);
    const char *key = flux_kvs_lookup_get_key (f);
    const char *R_lite;

    if (flux_kvs_lookup_get (f, &R_lite) < 0) {
        if (broker_rank == 0)
            flux_log (h, LOG_INFO, "No %s: %s", key, flux_strerror (errno));
        goto done;
    }
    if (!Rlite_targets_this_node (h, key, R_lite))
        goto done;

    if (spawn_exec_handler (h, job) < 0)
        goto done;
done:
    flux_future_destroy (f);
}

static int64_t id_from_tag (const char *tag)
{
    unsigned long l;
    char *p;
    errno = 0;
    l = strtoul (tag, &p, 10);
    if (*p != '\0')
        return (-1);
    if (l == 0 && errno == EINVAL)
        return (-1);
    else if (l == ULONG_MAX && errno == ERANGE)
        return (-1);
    return l;
}

/* Handle wrexec.run.<jobid> event.
 * Determine if assigned resources are on this broker rank, then spawn
 * wrexecd if so.   This function sends request to read R_lite,
 * then continues in runevent_continuation().
 */
static void runevent_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg,
                         void *arg)
{
    int64_t id;
    const char *topic;
    struct wreck_job *job;
    flux_future_t *f = NULL;
    char k[MAX_JOB_PATH];

    if (flux_event_decode (msg, &topic, NULL) < 0)
        goto error;
    id = id_from_tag (topic+11);
    if (!(job = wreck_job_lookup (id, active_jobs))) {
        errno = ENOENT;
        goto error;
    }
    if (!job->kvs_path)
        goto error;
    if (snprintf (k, sizeof (k), "%s.R_lite", job->kvs_path) >= sizeof (k)) {
        errno = EINVAL;
        goto error;
    }
    if (!(f = flux_kvs_lookup (h, 0, k)))
        goto error;
    if (flux_future_then (f, -1., runevent_continuation, job) < 0)
        goto error;
    /* N.B. 'f' and 'job' are destroyed by runevent_continuation() */
    return;
error:
    flux_log_error (h, "%s", __FUNCTION__);
    flux_future_destroy (f);
}

/* Track job state transition in active_jobs hash
 * Currently only id, kvs_path, and state are tracked.
 */
static void wreck_state_cb (flux_t *h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    int64_t id;
    const char *topic;
    const char *kvs_path = NULL;
    struct wreck_job *job;

    if (flux_event_unpack (msg, &topic, "{s:I s?s}",
                           "jobid", &id,
                           "kvs_path", &kvs_path) < 0)
        goto error;
    topic += 12; // state comes after "wreck.state." (12 chars)
    if (strlen (topic) == 0 || strlen (topic) >= sizeof (job->state)) {
        errno = EPROTO;
        goto error;
    }
    if (!(job = wreck_job_lookup (id, active_jobs))) {
        if (!(job = wreck_job_create ()))
            goto error;
        job->id = id;
        if (kvs_path && !(job->kvs_path = strdup (kvs_path)))
            goto error_destroy;
        if (wreck_job_insert (job, active_jobs) < 0)
            goto error_destroy;
    }
    wreck_job_set_state (job, topic);
    if ( !strcmp (job->state, "complete")
      || !strcmp (job->state, "failed")
      || !strcmp (job->state, "cancelled"))
        wreck_job_delete (id, active_jobs);
    return;
error_destroy:
    wreck_job_destroy (job);
error:
    flux_log_error (h, "%s", __FUNCTION__);
}

static void job_list_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    char *json_str = NULL;
    int max = 0;
    const char *include = NULL;
    const char *exclude = NULL;

    if (flux_request_unpack (msg, NULL, "{s?:i s?:s s?:s}",
                                        "max", &max,
                                        "include", &include,
                                        "exclude", &exclude) < 0)
        goto error;
    if (!(json_str = wreck_job_list (active_jobs, max, include, exclude)))
        goto error;
    if (flux_respond (h, msg, 0, json_str) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (json_str);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static const struct flux_msg_handler_spec mtab[] = {
    { FLUX_MSGTYPE_REQUEST, "job.create", job_create_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "job.submit", job_create_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "job.submit-nocreate", job_submit_only, 0 },
    { FLUX_MSGTYPE_REQUEST, "job.kvspath",  job_kvspath_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "job.list",  job_list_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "wrexec.run.*", runevent_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "wreck.state.*", wreck_state_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_msg_handler_t **handlers = NULL;
    int rc = -1;

    if (!(active_jobs = zhash_new ())) {
        flux_log_error (h, "zhash_new");
        return (-1);
    }
    if (flux_msg_handler_addvec (h, mtab, NULL, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto done;
    }
    if ((flux_event_subscribe (h, "wrexec.run.") < 0)) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if ((flux_event_subscribe (h, "wreck.state.") < 0)) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }

    if ((flux_attr_get_int (h, "wreck.lwj-dir-levels", &kvs_dir_levels) < 0)
       && (flux_attr_set_int (h, "wreck.lwj-dir-levels", kvs_dir_levels) < 0)) {
        flux_log_error (h, "failed to get or set lwj-dir-levels");
        goto done;
    }
    if ((flux_attr_get_int (h, "wreck.lwj-bits-per-dir",
                            &kvs_bits_per_dir) < 0)
       && (flux_attr_set_int (h, "wreck.lwj-bits-per-dir",
                              kvs_bits_per_dir) < 0)) {
        flux_log_error (h, "failed to get or set lwj-bits-per-dir");
        goto done;
    }

    if (flux_get_rank (h, &broker_rank) < 0) {
        flux_log_error (h, "flux_get_rank");
        goto done;
    }

    if (!(local_uri = flux_attr_get (h, "local-uri"))) {
        flux_log_error (h, "flux_attr_get (\"local-uri\")");
        goto done;
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (handlers);
    zhash_destroy (&active_jobs);
    return rc;
}

MOD_NAME ("job");

/*
 * vi: ts=4 sw=4 expandtab
 */
