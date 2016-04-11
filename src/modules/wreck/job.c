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
#include <zmq.h>
#include <czmq.h>
#include <inttypes.h>
#include <stdbool.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

static int kvs_job_set_state (flux_t h, unsigned long jobid, const char *state)
{
    int rc;
    char *key = NULL;
    char *link = NULL;
    char *target = NULL;

    /*  Create lwj entry in lwj-active dir at first:
     */
    if ((asprintf (&key, "lwj-active.%lu.state", jobid) < 0)
        || (asprintf (&link, "lwj.%lu", jobid) < 0)
        || (asprintf (&target, "lwj-active.%lu", jobid) < 0)) {
        flux_log_error (h, "kvs_job_set_state: asprintf");
        return (-1);
    }

    flux_log (h, LOG_INFO, "Setting job %ld to %s", jobid, state);
    if ((rc = kvs_put_string (h, key, state)) < 0) {
        flux_log_error (h, "kvs_put_string (%s)", key);
        goto out;
    }

    /*
     *  Create link from lwj.<id> to lwj-active.<id>
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

static int kvs_job_new (flux_t h, unsigned long jobid)
{
    return kvs_job_set_state (h, jobid, "reserved");
}

static int64_t next_jobid (flux_t h)
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

    if ((flux_rpc_get (rpc, NULL, &json_str) < 0)
        || !(resp = json_tokener_parse (json_str))) {
        flux_log (h, LOG_ERR, "rpc_get: %s\n", strerror (errno));
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

static void wait_for_event (flux_t h, int64_t id, char *topic)
{
    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_EVENT,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 0,
    };
    match.topic_glob = topic;
    flux_msg_t *msg = flux_recv (h, match, 0);
    flux_msg_destroy (msg);
    return;
}

static void send_create_event (flux_t h, int64_t id, char *topic)
{
    flux_msg_t *msg;
    char *json = NULL;

    if (asprintf (&json, "{\"lwj\":%ld}", id) < 0) {
        flux_log (h, LOG_ERR, "failed to create state change event: %s",
                  strerror (errno));
        goto out;
    }
    if ((msg = flux_event_encode (topic, json)) == NULL) {
        flux_log (h, LOG_ERR, "failed to create state change event: %s",
                  strerror (errno));
        goto out;
    }
    if (flux_send (h, msg, 0) < 0)
        flux_log (h, LOG_ERR, "reserved event failed: %s", strerror (errno));
    flux_msg_destroy (msg);

    /* Workaround -- wait for our own event to be published with a
     *  blocking recv. XXX: Remove when publish is synchronous.
     */
    wait_for_event (h, id, topic);
out:
    free (json);
}

static int add_jobinfo (flux_t h, int64_t id, json_object *req)
{
    int rc = 0;
    char buf [64];
    json_object_iter i;
    json_object *o;
    kvsdir_t *dir;

    if (kvs_get_dir (h, &dir, "lwj.%lu", id) < 0) {
        flux_log_error (h, "kvs_get_dir (id=%lu)", id);
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

static int wait_for_lwj_watch_init (flux_t h, int64_t id)
{
    int rc;
    const char *json_str;
    json_object *rpc_o;
    flux_rpc_t *rpc;

    rpc_o = Jnew ();
    Jadd_str (rpc_o, "key", "lwj.next-id");
    Jadd_int64 (rpc_o, "val", id);

    rpc = flux_rpc (h, "sim_sched.lwj-watch",
                    json_object_to_json_string (rpc_o),
                    FLUX_NODEID_ANY, 0);
    json_object_put (rpc_o);

    rc = flux_rpc_get (rpc, NULL, &json_str);
    if (rc >= 0) {
        json_object *rpc_resp = json_tokener_parse (json_str);
        if (rpc_resp) {
	        Jget_int (rpc_resp, "rc", &rc);
	        json_object_put (rpc_resp);
        }
    }
    return rc;
}

static bool ping_sched (flux_t h)
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
    if (flux_rpc_get (rpc, NULL, &s) >= 0)
        retval = true;
    flux_rpc_destroy (rpc);
    return (retval);
}

static bool sched_loaded (flux_t h)
{
    static bool v = false;
    if (!v && ping_sched (h))
        v = true;
    return (v);
}

static int do_submit_job (flux_t h, unsigned long id)
{
    if (kvs_job_set_state (h, id, "submitted") < 0) {
        flux_log (h, LOG_ERR, "kvs_job_set_state: %s\n", strerror (errno));
        return (-1);
    }
    send_create_event (h, id, "wreck.state.submitted");
    return (0);
}

static void job_request_cb (flux_t h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    json_object *o = NULL;
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0)
        goto out;
    flux_log (h, LOG_INFO, "got request %s", topic);
    if (flux_msg_get_payload_json (msg, &json_str) < 0)
        goto out;
    if (json_str && !(o = json_tokener_parse (json_str)))
        goto out;
    if (strcmp (topic, "job.shutdown") == 0) {
        flux_reactor_stop (flux_get_reactor (h));
    }
    else if ((strcmp (topic, "job.create") == 0)
            || ((strcmp (topic, "job.submit") == 0)
                 && sched_loaded (h))) {
        json_object *jobinfo = NULL;
        int64_t id;
        bool should_workaround = false;
        char *state;

        if ((id = next_jobid (h)) < 0) {
            if (flux_respond (h, msg, errno, NULL) < 0)
                flux_log (h, LOG_ERR, "job_request: flux_respond: %s",
                          strerror (errno));
            goto out;
        }

        //"Fix" for Race Condition
        if (!Jget_bool (o, "race_workaround", &should_workaround)) {
            should_workaround = false;
        } else if (should_workaround) {
            if (wait_for_lwj_watch_init (h, id) < 0) {
              flux_respond (h, msg, errno, NULL);
              goto out;
            }
        }

        if ( (kvs_job_new (h, id) < 0)
          || (add_jobinfo (h, id, o) < 0)) {
            flux_respond (h, msg, errno, NULL);
            goto out;
        }

        if (kvs_commit (h) < 0) {
            flux_log_error (h, "job_request: kvs_commit");
            goto out;
        }

        /* Send a wreck.state.reserved event for listeners */
        state = "reserved";
        send_create_event (h, id, "wreck.state.reserved");
        if ((strcmp (topic, "job.submit") == 0)
            && (do_submit_job (h, id) != -1))
                state = "submitted";

        /* Generate reply with new jobid */
        jobinfo = Jnew ();
        Jadd_int64 (jobinfo, "jobid", id);
        Jadd_str (jobinfo, "state", state);
        flux_respond (h, msg, 0, json_object_to_json_string (jobinfo));
        json_object_put (jobinfo);
    }
    else {
        /* job.submit not functional due to missing sched. Return ENOSYS
         *  for now
         */
        if (flux_respond (h, msg, ENOSYS, NULL) < 0)
            flux_log (h, LOG_ERR, "flux_respond: %s", strerror (errno));
    }

out:
    if (o)
        json_object_put (o);
}

struct flux_msg_handler_spec mtab[] = {
    { FLUX_MSGTYPE_REQUEST, "job.create", job_request_cb },
    { FLUX_MSGTYPE_REQUEST, "job.submit", job_request_cb },
    { FLUX_MSGTYPE_REQUEST, "job.shutdown", job_request_cb },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t h, int argc, char **argv)
{
    if (flux_msg_handler_addvec (h, mtab, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msg_handler_addvec: %s", strerror (errno));
        return (-1);
    }
    /* Subscribe to our own `wreck.state.reserved` events so we
     *  can verify the event has been published before responding to
     *  job.create requests.
     *
     * XXX: Remove when publish events are synchronous.
     */
    if ((flux_event_subscribe (h, "wreck.state.reserved") < 0)
       || (flux_event_subscribe (h, "wreck.state.submitted") < 0)) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_run: %s", strerror (errno));
        return -1;
    }
    flux_msg_handler_delvec (mtab);
    return 0;
}

MOD_NAME ("job");

/*
 * vi: ts=4 sw=4 expandtab
 */
