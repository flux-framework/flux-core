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

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/fdwalk.h"
#include "rcalc.h"

#define MAX_JOB_PATH    1024

struct job {
    int64_t id;
    char *kvs_path;
    char state[16];
    int nnodes;
    int ntasks;
    int ncores;
    int walltime;
};

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

static zhash_t *active_jobs = NULL;

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

static void job_destroy (struct job *job)
{
    if (job) {
        int saved_errno = errno;
        free (job->kvs_path);
        free (job);
        errno = saved_errno;
    }
}

static struct job *job_create (void)
{
    struct job *job;
    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    return job;
}

static void job_set_state (struct job *job, const char *status)
{
    assert (strlen (status) < sizeof (job->state));
    strcpy (job->state, status);
}

static struct job *job_create_newid (int64_t id, const char *status)
{
    struct job *job;

    if (!(job = job_create ()))
        return NULL;
    job->id = id;
    job_set_state (job, status);
    if (!(job->kvs_path = lwj_to_path (id, kvs_dir_levels, kvs_bits_per_dir))) {
        job_destroy (job);
        return NULL;
    }
    return job;
}

static int job_insert (struct job *job, zhash_t *hash)
{
    char key[16];
    int n;

    n = snprintf (key, sizeof (key), "%lld", (long long)job->id);
    assert (n < sizeof (key));
    if (zhash_lookup (hash, key) != NULL) {
        errno = EEXIST;
        return -1;
    }
    zhash_update (hash, key, job);
    zhash_freefn (hash, key, (zhash_free_fn *)job_destroy);
    return 0;
}

/* FIXME: make static once this has users */
struct job *job_lookup (int64_t id, zhash_t *hash)
{
    char key[16];
    int n;

    n = snprintf (key, sizeof (key), "%lld", (long long)id);
    assert (n < sizeof (key));
    return zhash_lookup (hash, key);
}

static void job_delete (int64_t id, zhash_t *hash)
{
    char key[16];
    int n;

    n = snprintf (key, sizeof (key), "%lld", (long long)id);
    assert (n < sizeof (key));
    zhash_delete (hash, key);
}

static int64_t next_jobid (flux_t *h)
{
    int64_t ret = (int64_t) -1;
    flux_future_t *f;

    f = flux_rpc_pack (h, "seq.fetch", 0, 0, "{s:s,s:i,s:i,s:b}",
                       "name", "lwj",
                       "preincrement", 1,
                       "postincrement", 0,
                       "create", true);
    if (f == NULL) {
        flux_log_error (h, "next_jobid: flux_rpc");
        goto out;
    }
    if ((flux_rpc_get_unpack (f, "{s:I}", "value", &ret)) < 0) {
        flux_log_error (h, "rpc_get_unpack");
        goto out;
    }
out:
    flux_future_destroy (f);
    return ret;
}

struct job *job_create_next (flux_t *h, const char *status)
{
    int64_t id;

    if ((id = next_jobid (h)) < 0)
        return NULL;
    return job_create_newid (id, status);
}

static char * realtime_string (char *buf, size_t sz)
{
    struct timespec tm;
    clock_gettime (CLOCK_REALTIME, &tm);
    memset (buf, 0, sz);
    snprintf (buf, sz, "%ju.%06ld", (uintmax_t) tm.tv_sec, tm.tv_nsec/1000);
    return (buf);
}

static void wait_for_event (flux_t *h, int64_t id, char *topic)
{
    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_EVENT,
        .matchtag = FLUX_MATCHTAG_NONE,
    };
    match.topic_glob = topic;
    flux_msg_t *msg = flux_recv (h, match, 0);
    flux_msg_destroy (msg);
    return;
}

static int send_create_event (flux_t *h, struct job *job)
{
    char topic[64];
    flux_msg_t *msg;
    int n;
    int rc = -1;

    n = snprintf (topic, sizeof (topic), "wreck.state.%s", job->state);
    assert (n < sizeof (topic));

    msg = flux_event_pack (topic, "{s:I,s:s,s:i,s:i,s:i,s:i}",
                          "jobid", job->id,
                          "kvs_path", job->kvs_path,
                          "ntasks", job->ntasks,
                          "ncores", job->ncores,
                          "nnodes", job->nnodes,
                          "walltime", job->walltime);

    if (msg == NULL) {
        flux_log_error (h, "failed to create state change event");
        goto done;
    }
    if (flux_send (h, msg, 0) < 0) {
        flux_log_error (h, "create_event: flux_send");
        goto done;
    }

    /* Workaround -- wait for our own event to be published with a
     *  blocking recv. XXX: Remove when publish is synchronous.
     */
    wait_for_event (h, job->id, topic);
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
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

static int add_jobinfo_txn (flux_kvs_txn_t *txn,
                            struct job *job, const flux_msg_t *msg)
{
    char k[MAX_JOB_PATH];
    char timebuf[64];
    const char *json_str;
    const char *val;
    json_object_iter itr;
    json_object *o = NULL;

    /* Set job state and create-time.
     */
    if (snprintf (k, sizeof (k), "%s.state", job->kvs_path) >= sizeof (k))
        goto inval;
    if (flux_kvs_txn_pack (txn, 0, k, "s", job->state) < 0)
        goto error;
    if (snprintf (k, sizeof (k), "%s.create-time", job->kvs_path) >= sizeof (k))
        goto inval;
    val = realtime_string (timebuf, sizeof (timebuf));
    if (flux_kvs_txn_pack (txn, 0, k, "s", val) < 0)
        goto error;
    /* transfer all request payload object key-value pairs to txn
     */
    if (flux_msg_get_json (msg, &json_str) < 0)
        goto error;
    if (!json_str || !(o = json_tokener_parse (json_str)))
        goto inval;
    json_object_object_foreachC (o, itr) {
        if (snprintf (k, sizeof (k),
                      "%s.%s", job->kvs_path, itr.key) >= sizeof (k))
            goto inval;
        val = json_object_to_json_string (itr.val);
        if (flux_kvs_txn_put (txn, 0, k, val) < 0)
            goto error;
    }
    json_object_put (o);
    return 0;
inval:
    errno = EINVAL;
error:
    if (o) {
        int saved_errno = errno;
        json_object_put (o);
        errno = saved_errno;
    }
    return -1;
}

/* Handle job creation.
 * The request payload is a flat JSON object that is transferred
 * verbatim to the job's KVS directory without validation.
 * In addition, ntasks, nnodes, ncores, and walltime are passed through
 * to the wreck.state.<state> event (with default value of 0 if not set).
 */
static void handle_job_create (flux_t *h, const flux_msg_t *msg,
                               const char *state)
{
    struct job *job;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!(job = job_create_next (h, state))) {
        flux_log_error (h, "%s: job_create_next", __FUNCTION__);
        goto error;
    }
    if (flux_request_unpack (msg, NULL, "{s?:i s?:i s?:i s?:i}",
                                        "ntasks", &job->ntasks,
                                        "nnodes", &job->nnodes,
                                        "ncores", &job->ncores,
                                        "walltime", &job->walltime) < 0)
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (add_jobinfo_txn (txn, job, msg) < 0)
        goto error;
    flux_log (h, LOG_DEBUG, "Setting job %ld to %s", job->id, job->state);
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0)
        goto error;
    if (send_create_event (h, job) < 0)
        goto error;
    if (job_insert (job, active_jobs) < 0)
        goto error;
    // 'job' is property of the 'active_jobs' hash now, do not destroy
    if (flux_respond_pack (h, msg, "{s:I,s:s,s:s}",
                                   "jobid", job->id,
                                   "state", job->state,
                                   "kvs_path", job->kvs_path) < 0)
        flux_log_error (h, "flux_respond_pack");
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "job_request: flux_respond");
    job_destroy (job);
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
}

/* Handle job.submit-nocreate request from 'flux wreckrun'.
 * If sched is loaded, use it to assign resources to interactive job.
 * If sched is not loaded, wreckrun will fall back to job.create.
 */
static void job_submit_nocreate (flux_t *h, flux_msg_handler_t *w,
                                 const flux_msg_t *msg, void *arg)
{
    struct job *job = NULL;

    if (broker_rank > 0 || !sched_loaded (h)) {
        errno = ENOSYS;
        goto error;
    }
    if (!(job = job_create ()))
        goto error;
    if (flux_request_unpack (msg, NULL, "{s:I s:s s?:i s?:i s?:i s?:i}",
                                        "jobid", &job->id,
                                        "kvs_path", &job->kvs_path,
                                        "ntasks", &job->ntasks,
                                        "nnodes", &job->nnodes,
                                        "ncores", &job->ncores,
                                        "walltime", &job->walltime) < 0)
        goto error;
    job_set_state (job, "submitted");

    if (send_create_event (h, job) < 0)
        goto error;
    if (job_insert (job, active_jobs) < 0)
        goto error;
    // 'job' is property of the 'active_jobs' hash now, do not destroy
    if (flux_respond_pack (h, msg, "{s:I}", "jobid", job->id) < 0)
        flux_log_error (h, "flux_respond");
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "flux_respond");
    job_destroy (job);
}

/* Handle job.submit request from 'flux submit'.
 */
static void job_submit_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    if (broker_rank > 0 || !sched_loaded (h)) {
        if (flux_respond (h, msg, ENOSYS, NULL) < 0)
            flux_log_error (h, "flux_respond");
        return;
    }
    handle_job_create (h, msg, "submitted");
}

/* Handle job.create request from 'flux wreckrun --immediate'.
 */
static void job_create_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    if (broker_rank > 0) {
        if (flux_respond (h, msg, ENOSYS, NULL) < 0)
            flux_log_error (h, "flux_respond");
        return;
    }
    handle_job_create (h, msg, "reserved");
}

static void job_kvspath_cb (flux_t *h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    int errnum = EPROTO;
    int i, n;
    const char *json_str;
    json_object *in = NULL;
    json_object *out = NULL;
    json_object *ar = NULL;
    json_object *id_list = NULL;

    if (flux_msg_get_json (msg, &json_str) < 0)
        goto out;

    if (!(in = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "kvspath_cb: Failed to parse JSON string");
        goto out;
    }

    if (!Jget_obj (in, "ids", &id_list)) {
        flux_log (h, LOG_ERR, "kvspath_cb: required key ids missing");
        goto out;
    }

    if (!json_object_is_type (id_list, json_type_array)) {
        errno = EPROTO;
        goto out;
    }

    if (!(out = json_object_new_object ())
        || !(ar = json_object_new_array ())) {
        flux_log (h, LOG_ERR, "kvspath_cb: json_object_new_object failed");
        goto out;
    }

    errnum = ENOMEM;
    n = json_object_array_length (id_list);
    for (i = 0; i < n; i++) {
        json_object *r;
        json_object *v = json_object_array_get_idx (id_list, i);
        int64_t id = json_object_get_int64 (v);
        char * path;
        if (!(path = id_to_path (id))) {
            flux_log (h, LOG_ERR, "kvspath_cb: lwj_to_path failed");
            goto out;
        }
        r = json_object_new_string (path);
        free (path);
        if (r == NULL) {
            flux_log_error (h, "kvspath_cb: json_object_new_string");
            goto out;
        }
        json_object_array_add (ar, r);
    }
    json_object_object_add (out, "paths", ar);
    ar = NULL; /* allow Jput below */
    errnum = 0;
out:
    if (flux_respond (h, msg, errnum, out ? Jtostr (out) : NULL) < 0)
        flux_log_error (h, "kvspath_cb: flux_respond");
    Jput (in);
    Jput (ar);
    Jput (out);
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

    if ((tmp = flux_attr_get (h, attr, 0)) == NULL)
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

static void exec_close_fd (void *arg, int fd)
{
    if (fd >= 3)
        (void) close (fd);
}

static void exec_handler (const char *exe, int64_t id, const char *kvspath)
{
    pid_t sid;
    int argc = 2;
    char **av = malloc ((sizeof (char *)) * (argc + 2));

    if ((av == NULL)
     || ((av [0] = strdup (exe)) == NULL)
     || (asprintf (&av[1], "--lwj-id=%"PRId64, id) < 0)
     || (asprintf (&av[2], "--kvs-path=%s", kvspath) < 0)) {
        fprintf (stderr, "Out of Memory trying to exec wrexecd!\n");
        exit (1);
    }
    av[argc+1] = NULL;

    if ((sid = setsid ()) < 0)
        fprintf (stderr, "setsid: %s\n", strerror (errno));

    /*
     *  NOTE: There used to be a double fork here, presumably to
     *  "daemonize" wrexecd, however I'm not sure that is warranted
     *   nor even advisable. With the setsid above, the wrexecd
     *   process should be reparented to init.
     */

    fdwalk (exec_close_fd, NULL);
    if (setenv ("FLUX_URI", local_uri, 1) < 0)
        fprintf (stderr, "setenv: %s\n", strerror (errno));
    else if (execvp (av[0], av) < 0)
        fprintf (stderr, "wrexecd exec: %s\n", strerror (errno));
    exit (255);
}

static int spawn_exec_handler (flux_t *h, int64_t id, const char *kvspath)
{
    pid_t pid;
    const char *wrexecd_path;

    if (!(wrexecd_path = flux_attr_get (h, "wrexec.wrexecd_path", NULL))) {
        flux_log_error (h, "spawn_exec_handler: flux_attr_get");
        return (-1);
    }

    if ((pid = fork ()) < 0) {
        flux_log_error (h, "spawn_exec_handler: fork");
        return (-1);
    }

    if (pid == 0) {
#if WITH_TCMALLOC
        /* Child: if heap profiling is running, stop it to avoid
         * triggering a dump when child exits.
         */
        if (IsHeapProfilerRunning ())
            HeapProfilerStop ();
#endif
        exec_handler (wrexecd_path, id, kvspath);
    }

    // XXX: Add child watcher for pid

    return (0);
}

static bool lwj_targets_this_node (flux_t *h, const char *kvspath)
{
    char key[MAX_JOB_PATH];
    flux_future_t *f = NULL;
    const flux_kvsdir_t *dir;
    bool result = false;

    snprintf (key, sizeof (key), "%s.rank", kvspath);
    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key))
            || flux_kvs_lookup_get_dir (f, &dir) < 0) {
        flux_log (h, LOG_DEBUG, "No dir %s.rank: %s",
                  kvspath, flux_strerror (errno));
        goto done;
    }
    snprintf (key, sizeof (key), "%d", broker_rank);
    if (flux_kvsdir_isdir (dir, key))
        result = true;
done:
    flux_future_destroy (f);
    return result;
}

static bool Rlite_targets_this_node (flux_t *h, const char *kvspath)
{
    const char *R_lite;
    rcalc_t *r = NULL;
    char key[MAX_JOB_PATH];
    flux_future_t *f = NULL;
    bool result = false;

    snprintf (key, sizeof (key), "%s.R_lite", kvspath);
    if (!(f = flux_kvs_lookup (h, 0, key))
       || flux_kvs_lookup_get (f, &R_lite) < 0)  {
        if (broker_rank == 0)
            flux_log (h, LOG_INFO, "No %s.R_lite: %s",
                      kvspath, flux_strerror (errno));
        goto done;
    }
    if (!(r = rcalc_create (R_lite))) {
        if (broker_rank == 0)
            flux_log (h, LOG_ERR, "Unable to parse %s.R_lite", kvspath);
        goto done;
    }
    if (rcalc_has_rank (r, broker_rank))
        result = true;
    rcalc_destroy (r);
done:
    flux_future_destroy (f);
    return result;
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

static void runevent_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg,
                         void *arg)
{
    const char *topic;
    char *kvspath = NULL;
    json_object *in = NULL;
    int64_t id = -1;

    if (flux_msg_get_topic (msg, &topic) < 0) {
        flux_log_error (h, "run: flux_msg_get_topic");
        return;
    }
    if ((id = id_from_tag (topic+11)) < 0) {
        flux_log_error (h, "wrexec.run: invalid topic: %s\n", topic);
        return;
    }
    kvspath = id_to_path (id);
    if (Rlite_targets_this_node (h, kvspath)
       || lwj_targets_this_node (h, kvspath))
        spawn_exec_handler (h, id, kvspath);
    free (kvspath);
    Jput (in);
}

/* (rank 0 only) job entered terminal state (complete or failed)
 */
static void finevent_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    int64_t jobid;
    const char *kvs_path;

    if (flux_event_unpack (msg, NULL, "{ s:I s:s }",
                                        "jobid", &jobid,
                                        "kvs_path", &kvs_path) < 0) {
        flux_log_error (h, "%s: flux_event_decode", __FUNCTION__);
        return;
    }
    job_delete (jobid, active_jobs);
}

static const struct flux_msg_handler_spec mtab[] = {
    { FLUX_MSGTYPE_REQUEST, "job.create", job_create_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "job.submit", job_submit_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "job.submit-nocreate", job_submit_nocreate, 0 },
    { FLUX_MSGTYPE_REQUEST, "job.kvspath",  job_kvspath_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "wrexec.run.*", runevent_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "wreck.state.complete", finevent_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "wreck.state.failed", runevent_cb, 0 },
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
        return (-1);
    }
    /* Subscribe to our own `wreck.state.reserved` events so we
     *  can verify the event has been published before responding to
     *  job.create requests.
     *
     * XXX: Remove when publish events are synchronous.
     */
    if ((flux_event_subscribe (h, "wreck.state.reserved") < 0)
       || (flux_event_subscribe (h, "wreck.state.submitted") < 0)
       || (flux_event_subscribe (h, "wrexec.run.") < 0)) {
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
    if (broker_rank == 0) {
       if ((flux_event_subscribe (h, "wreck.state.complete") < 0)
            || (flux_event_subscribe (h, "wreck.state.failed") < 0)) {
           flux_log_error (h, "flux_event_subscribe");
           goto done;
       }
    }

    if (!(local_uri = flux_attr_get (h, "local-uri", NULL))) {
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
