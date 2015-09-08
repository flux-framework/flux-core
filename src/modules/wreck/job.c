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
#include "src/common/libutil/jsonutil.h"

#if 0
zlist_t * kvs_jobid_list (plugin_ctx_t *p)
{
    zlist_t *zl = NULL;
    json_object_iter i;
    json_object *val = NULL;
    json_object *reply = NULL;
    json_object *request = util_json_object_new_object ();

    json_object_object_add (request, "lwj.id-list", NULL);

    reply = flux_rpc (p, request, "kvs.get.dir");
    if (!reply) {
        err ("kvs_job_list: plugin request failed!");
        goto done;
    }
    if (!(val = json_object_object_get (reply, "lwj.ids"))) {
        errno = ENOENT;
        err ("kvs_job_list: %s", strerror (errno));
        goto done;
    }
    if (!(zl = zlist_new ())) {
        errno = ENOMEM;
        goto done;
    }

    json_object_object_foreachC (val, i) {
        json_object *co;
        if ((co = json_object_object_get (i.val, "FILEVAL"))) {
            zlist_append (zl,
                strdup (json_object_to_json_string_ext (co,
                            JSON_C_TO_STRING_PLAIN)));
        }
    }

done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);

    return (zl);
}

void jobid_list_free (zlist_t *list)
{
    void *s;
    while ((s = zlist_pop (list)))
        free (s);
    zlist_destroy (&list);
}

#endif

static int kvs_job_new (flux_t h, unsigned long jobid)
{
    int rc;
    char *key;

    if (asprintf (&key, "lwj.%lu.state", jobid) < 0)
        return (-1);

    flux_log (h, LOG_INFO, "Setting job %ld to reserved", jobid);
    rc = kvs_put_string (h, key, "reserved");
    kvs_commit (h);

    free (key);
    return rc;
}

/*
 *  Set a new value for lwj.next-id and commit
 */
static int set_next_jobid (flux_t h, unsigned long jobid)
{
    int rc;
    if ((rc = kvs_put_int64 (h, "lwj.next-id", jobid)) < 0) {
        err ("kvs_put: %s", strerror (errno));
        return -1;
    }
    return kvs_commit (h);
}

/*
 *  Get and increment lwj.next-id (called from treeroot only)
 */
static unsigned long increment_jobid (flux_t h)
{
    int64_t ret;
    int rc;
    rc = kvs_get_int64 (h, "lwj.next-id", &ret);
    if (rc < 0 && (errno == ENOENT))
        ret = 1;
    set_next_jobid (h, ret+1);
    return (unsigned long) ret;
}

/*
 *  Tree-wide call for lwj_next_id. If not treeroot forward request
 *   up the tree. Otherwise increment jobid and return result.
 */
static unsigned long lwj_next_id (flux_t h)
{
    unsigned long ret;
    uint32_t rank;
    if (flux_get_rank (h, &rank) < 0) {
        flux_log (h, LOG_ERR, "flux_get_rank: %s", strerror (errno));
        return (0);
    }
    if (rank == 0)
        ret = increment_jobid (h);
    else {
        const char *s;
        json_object *o = NULL;
        flux_rpc_t *rpc = flux_rpc (h, "job.next-id", NULL, FLUX_NODEID_ANY, 0);
        if (!rpc) {
            flux_log (h, LOG_ERR, "flux_rpc: %s", strerror (errno));
            return (0);
        }
        if (flux_rpc_get (rpc, NULL, &s) < 0) {
            flux_log (h, LOG_ERR, "flux_rpc: %s", strerror (errno));
            return (0);
        }
        o = json_tokener_parse (s);
        if (!o || util_json_object_get_int64 (o, "id", (int64_t *) &ret) < 0) {
            flux_log (h, LOG_ERR, "lwj_next_id: Bad object!");
            ret = 0;
        }
        if (o)
            json_object_put (o);
    }
    return (ret);
}

/*
 *  create json object : { id: <int64>id }
 */
static json_object *json_id (int id)
{
    json_object *o = json_object_new_object ();
    util_json_object_add_int64 (o, "id", id);
    return (o);
}

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

static void wait_for_event (flux_t h, int64_t id)
{
    struct flux_match match = {
        .typemask = FLUX_MSGTYPE_EVENT,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 0,
        .topic_glob = "wreck.state.reserved"
    };
    flux_msg_t *msg = flux_recv (h, match, 0);
    flux_msg_destroy (msg);
    return;
}

static void send_create_event (flux_t h, int64_t id)
{
    flux_msg_t *msg;
    char *json = NULL;
    const char *topic = "wreck.state.reserved";

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
    wait_for_event (h, id);
out:
    free (json);
}

static void add_jobinfo (flux_t h, int64_t id, json_object *req)
{
    char buf [64];
    json_object_iter i;
    json_object *o;
    kvsdir_t *dir;

    if (kvs_get_dir (h, &dir, "lwj.%lu", id) < 0)
        err_exit ("kvs_get_dir (id=%lu)", id);

    json_object_object_foreachC (req, i)
        kvsdir_put (dir, i.key, json_object_to_json_string (i.val));

    o = json_object_new_string (ctime_iso8601_now (buf, sizeof (buf)));
    kvsdir_put (dir, "create-time", json_object_to_json_string (o));
    json_object_put (o);

    kvsdir_destroy (dir);
}

static int wait_for_lwj_watch_init (flux_t h, int64_t id)
{
    int rc;
    const char *json_str;
    json_object *rpc_o;
    flux_rpc_t *rpc;

    rpc_o = util_json_object_new_object ();
    util_json_object_add_string (rpc_o, "key", "lwj.next-id");
    util_json_object_add_int64 (rpc_o, "val", id);

    rpc = flux_rpc (h, "sim_sched.lwj-watch",
                    json_object_to_json_string (rpc_o),
                    FLUX_NODEID_ANY, 0);
    json_object_put (rpc_o);

    rc = flux_rpc_get (rpc, NULL, &json_str);
    if (rc >= 0) {
        json_object *rpc_resp = json_tokener_parse (json_str);
        if (rpc_resp) {
	    util_json_object_get_int (rpc_resp, "rc", &rc);
	    json_object_put (rpc_resp);
        }
    }
    return rc;
}

static void job_request_cb (flux_t h, flux_msg_watcher_t *w,
                           const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    json_object *o = NULL;
    const char *topic;
    uint32_t rank;

    if (flux_get_rank (h, &rank) < 0)
        goto out;
    if (flux_msg_get_topic (msg, &topic) < 0)
        goto out;
    flux_log (h, LOG_INFO, "got request %s", topic);
    if (flux_msg_get_payload_json (msg, &json_str) < 0)
        goto out;
    if (json_str && !(o = json_tokener_parse (json_str)))
        goto out;
    if (strcmp (topic, "job.shutdown") == 0) {
        flux_reactor_stop (h);
    }
    if (strcmp (topic, "job.next-id") == 0) {
        if (rank == 0) {
            unsigned long id = lwj_next_id (h);
            json_object *ox = json_id (id);
            if (flux_respond (h, msg, 0, json_object_to_json_string (ox)) < 0)
                flux_log (h, LOG_ERR, "flux_respond (job.next-id): %s", strerror (errno));
            json_object_put (ox);
        }
        else {
            if (flux_send (h, msg, FLUX_NODEID_ANY) < 0)
                flux_log (h, LOG_ERR, "error forwarding next-id request: %s",
                          strerror (errno));
        }
    }
    if (strcmp (topic, "job.create") == 0) {
        json_object *jobinfo = NULL;
        unsigned long id = lwj_next_id (h);
        bool should_workaround = false;

        //"Fix" for Race Condition
        if (util_json_object_get_boolean (o, "race_workaround",
                                           &should_workaround) < 0) {
            should_workaround = false;
        } else if (should_workaround) {
            if (wait_for_lwj_watch_init (h, id) < 0) {
              flux_respond (h, msg, errno, NULL);
              goto out;
            }
        }

        int rc = kvs_job_new (h, id);
        if (rc < 0) {
            flux_respond (h, msg, errno, NULL);
            goto out;
        }
        add_jobinfo (h, id, o);

        kvs_commit (h);

        /* Send a wreck.state.reserved event for listeners */
        send_create_event (h, id);

        /* Generate reply with new jobid */
        jobinfo = util_json_object_new_object ();
        util_json_object_add_int64 (jobinfo, "jobid", id);
        flux_respond (h, msg, 0, json_object_to_json_string (jobinfo));
        json_object_put (jobinfo);
    }

out:
    if (o)
        json_object_put (o);
}

struct flux_msghandler mtab[] = {
    { FLUX_MSGTYPE_REQUEST, "job.*", job_request_cb },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t h, int argc, char **argv)
{
    if (flux_msg_watcher_addvec (h, mtab, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msg_watcher_addvec: %s", strerror (errno));
        return (-1);
    }
    /* Subscribe to our own `wreck.state.reserved` events so we
     *  can verify the event has been published before responding to
     *  job.create requests.
     *
     * XXX: Remove when publish events are synchronous.
     */
    if (flux_event_subscribe (h, "wreck.state.reserved") < 0) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    flux_msg_watcher_delvec (h, mtab);
    return 0;
}

MOD_NAME ("job");

/*
 * vi: ts=4 sw=4 expandtab
 */
