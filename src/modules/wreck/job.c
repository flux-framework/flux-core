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

/*
 *  lwj-active directory tree parameters:
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
    char buf [1024] = "lwj-active";
    int len = 10;
    int nleft = sizeof (buf) - len;
    int i, n;

    /* Build up kvs directory from lwj-active. down */
    for (i = levels; i > 0; i--) {
        int b = bits_per_dir * i;
        uint64_t d = prefix64 (id, b, b + bits_per_dir);
        if ((n = snprintf (buf+len, nleft, ".%ju", d)) < 0 || n > nleft)
            return NULL;
        len += n;
        nleft -= n;
    }
    n = snprintf (buf+len, sizeof (buf) - len, ".%ju", id);
    if (n < 0 || n > nleft)
        return NULL;
    return (strdup (buf));
}

static char * id_to_path (uint64_t id)
{
    return (lwj_to_path (id, kvs_dir_levels, kvs_bits_per_dir));
}

static int kvs_job_set_state (flux_t *h, unsigned long jobid, const char *state)
{
    int rc = -1;
    char *key = NULL;
    char *link = NULL;
    char *target = NULL;

    if (!(target = id_to_path (jobid))) {
        flux_log_error (h, "lwj_to_path");
        return (-1);
    }

    /*  Create lwj entry in lwj-active dir at first:
     */
    if ((asprintf (&key, "%s.state", target) < 0)
        || (asprintf (&link, "lwj.%lu", jobid) < 0)) {
        flux_log_error (h, "kvs_job_set_state: asprintf");
        goto out;
    }

    flux_log (h, LOG_DEBUG, "Setting job %ld to %s", jobid, state);
    if ((rc = kvs_put_string (h, key, state)) < 0) {
        flux_log_error (h, "kvs_put_string (%s)", key);
        goto out;
    }

    /*
     *  Create link from lwj.<id> to lwj-active.*.<id>
     */
    if ((rc = kvs_symlink (h, link, target)) < 0) {
        flux_log_error (h, "kvs_symlink (%s, %s)", link, key);
        goto out;
    }

    if ((rc = kvs_commit (h)) < 0)
        flux_log_error (h, "kvs_job_set_state: kvs_commit");

out:
    free (key);
    free (link);
    free (target);
    return rc;
}

static int kvs_job_new (flux_t *h, unsigned long jobid)
{
    return kvs_job_set_state (h, jobid, "reserved");
}

static int64_t next_jobid (flux_t *h)
{
    int64_t ret = (int64_t) -1;
    const char *json_str;
    json_object *req, *resp;
    flux_rpc_t *rpc;

    req = Jnew ();
    Jadd_str (req, "name", "lwj");
    Jadd_int64 (req, "preincrement", 1);
    Jadd_int64 (req, "postincrement", 0);
    Jadd_bool (req, "create", true);
    rpc = flux_rpc (h, "cmb.seq.fetch",
                    json_object_to_json_string (req), 0, 0);
    json_object_put (req);

    if ((flux_rpc_get (rpc, &json_str) < 0)
        || !(resp = json_tokener_parse (json_str))) {
        flux_log_error (h, "rpc_get");
        goto out;
    }

    Jget_int64 (resp, "value", &ret);
    json_object_put (resp);
out:
    flux_rpc_destroy (rpc);
    return ret;
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

static void send_create_event (flux_t *h, int64_t id,
                               const char *path, char *topic)
{
    flux_msg_t *msg;
    json_object *o = Jnew ();

    Jadd_int64 (o, "lwj", id);
    Jadd_str (o, "kvs_path", path);

    if ((msg = flux_event_encode (topic, Jtostr (o))) == NULL) {
        flux_log_error (h, "failed to create state change event");
        goto out;
    }
    if (flux_send (h, msg, 0) < 0)
        flux_log_error (h, "reserved event failed");
    flux_msg_destroy (msg);

    /* Workaround -- wait for our own event to be published with a
     *  blocking recv. XXX: Remove when publish is synchronous.
     */
    wait_for_event (h, id, topic);
out:
    Jput (o);
}

static int add_jobinfo (flux_t *h, const char *kvspath, json_object *req)
{
    int rc = 0;
    char buf [64];
    json_object_iter i;
    json_object *o;
    kvsdir_t *dir;

    if (kvs_get_dir (h, &dir, "%s", kvspath) < 0) {
        flux_log_error (h, "kvs_get_dir (%s)", kvspath);
        return (-1);
    }

    json_object_object_foreachC (req, i) {
        rc = kvsdir_put (dir, i.key, json_object_to_json_string (i.val));
        if (rc < 0) {
            flux_log_error (h, "addd_jobinfo: kvsdir_put (key=%s)", i.key);
            goto out;
        }
    }

    o = json_object_new_string (realtime_string (buf, sizeof (buf)));
    /* Not a fatal error if create-time addition fails */
    if (kvsdir_put (dir, "create-time", json_object_to_json_string (o)) < 0)
        flux_log_error (h, "add_jobinfo: kvsdir_put (create-time)");
    json_object_put (o);

out:
    kvsdir_destroy (dir);
    return (rc);
}

static bool ping_sched (flux_t *h)
{
    bool retval = false;
    const char *s;
    flux_rpc_t *rpc;
    json_object *o = Jnew ();
    Jadd_int (o, "seq", 0);
    rpc = flux_rpc (h, "sched.ping",
                    json_object_to_json_string (o),
                    FLUX_NODEID_ANY, 0);
    json_object_put (o);
    if (flux_rpc_get (rpc, &s) >= 0)
        retval = true;
    flux_rpc_destroy (rpc);
    return (retval);
}

static bool sched_loaded (flux_t *h)
{
    static bool v = false;
    if (!v && ping_sched (h))
        v = true;
    return (v);
}

static int do_submit_job (flux_t *h, unsigned long id, const char *path)
{
    if (kvs_job_set_state (h, id, "submitted") < 0) {
        flux_log_error (h, "kvs_job_set_state");
        return (-1);
    }
    send_create_event (h, id, path, "wreck.state.submitted");
    return (0);
}

static void handle_job_create (flux_t *h, const flux_msg_t *msg,
                               const char *topic, json_object *o)
{
    json_object *jobinfo = NULL;
    int64_t id;
    char *state;
    char *kvs_path;

    if ((id = next_jobid (h)) < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "job_request: flux_respond");
        return;
    }

    asprintf (&kvs_path, "lwj.%ju", (uintmax_t) id);

    if ( (kvs_job_new (h, id) < 0)
      || (add_jobinfo (h, kvs_path, o) < 0)) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "job_request: flux_respond");
        return;
    }

    if (kvs_commit (h) < 0) {
        flux_log_error (h, "job_request: kvs_commit");
        return;
    }

    /* Send a wreck.state.reserved event for listeners */
    state = "reserved";
    send_create_event (h, id, kvs_path, "wreck.state.reserved");
    if ((strcmp (topic, "job.submit") == 0)
            && (do_submit_job (h, id, kvs_path) != -1))
        state = "submitted";


    /* Generate reply with new jobid */
    jobinfo = Jnew ();
    Jadd_int64 (jobinfo, "jobid", id);
    Jadd_str (jobinfo, "state", state);
    Jadd_str (jobinfo, "kvs_path", kvs_path);
    flux_respond (h, msg, 0, json_object_to_json_string (jobinfo));
    Jput (jobinfo);
    free (kvs_path);
}

static void job_request_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    json_object *o = NULL;
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0)
        goto out;
    flux_log (h, LOG_DEBUG, "got request %s", topic);
    if (flux_msg_get_payload_json (msg, &json_str) < 0)
        goto out;
    if (json_str && !(o = json_tokener_parse (json_str)))
        goto out;
    if (strcmp (topic, "job.shutdown") == 0) {
        flux_reactor_stop (flux_get_reactor (h));
    }
    else if ((strcmp (topic, "job.create") == 0)
            || ((strcmp (topic, "job.submit") == 0)
                 && sched_loaded (h)))
        handle_job_create (h, msg, topic, o);
    else {
        /* job.submit not functional due to missing sched. Return ENOSYS
         *  for now
         */
        if (flux_respond (h, msg, ENOSYS, NULL) < 0)
            flux_log_error (h, "flux_respond");
    }

out:
    if (o)
        json_object_put (o);
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

    if (flux_msg_get_payload_json (msg, &json_str) < 0)
        goto out;

    if (!(in = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "kvspath_cb: Failed to parse JSON string");
        goto out;
    }

    if (!Jget_obj (in, "ids", &id_list)) {
        flux_log (h, LOG_ERR, "kvspath_cb: required key ids missing");
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
        if (!(r = json_object_new_string (path))) {
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
     || (asprintf (&av[1], "--lwj-id=%ju", (uintmax_t) id) < 0)
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
    kvsdir_t *tmp;
    /*
     *  If no 'rank' subdir exists for this lwj, then we are running
     *   without resource assignment so we run everywhere
     */
    if (kvs_get_dir (h, &tmp, "%s.rank", kvspath) < 0) {
        flux_log (h, LOG_INFO, "No dir %s.rank: %s",
                  kvspath, strerror (errno));
        return (true);
    }
    kvsdir_destroy (tmp);
    if (kvs_get_dir (h, &tmp, "%s.rank.%d", kvspath, broker_rank) < 0)
        return (false);
    kvsdir_destroy (tmp);
    return (true);
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
    if (lwj_targets_this_node (h, kvspath))
        spawn_exec_handler (h, id, kvspath);
    free (kvspath);
    Jput (in);
}

struct flux_msg_handler_spec mtab[] = {
    { FLUX_MSGTYPE_REQUEST, "job.create", job_request_cb },
    { FLUX_MSGTYPE_REQUEST, "job.submit", job_request_cb },
    { FLUX_MSGTYPE_REQUEST, "job.shutdown", job_request_cb },
    { FLUX_MSGTYPE_REQUEST, "job.kvspath",  job_kvspath_cb },
    { FLUX_MSGTYPE_EVENT,   "wrexec.run.*", runevent_cb },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    if (flux_msg_handler_addvec (h, mtab, NULL) < 0) {
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
        return -1;
    }

    if ((flux_attr_get_int (h, "wreck.lwj-dir-levels", &kvs_dir_levels) < 0)
       && (flux_attr_set_int (h, "wreck.lwj-dir-levels", kvs_dir_levels) < 0)) {
        flux_log_error (h, "failed to get or set lwj-dir-levels");
        return -1;
    }
    if ((flux_attr_get_int (h, "wreck.lwj-bits-per-dir",
                            &kvs_bits_per_dir) < 0)
       && (flux_attr_set_int (h, "wreck.lwj-bits-per-dir",
                              kvs_bits_per_dir) < 0)) {
        flux_log_error (h, "failed to get or set lwj-bits-per-dir");
        return -1;
    }

    if (flux_get_rank (h, &broker_rank) < 0) {
        flux_log_error (h, "flux_get_rank");
        return -1;
    }

    if (!(local_uri = flux_attr_get (h, "local-uri", NULL))) {
        flux_log_error (h, "flux_attr_get (\"local-uri\")");
        return -1;
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        return -1;
    }
    flux_msg_handler_delvec (mtab);
    return 0;
}

MOD_NAME ("job");

/*
 * vi: ts=4 sw=4 expandtab
 */
