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
    if (flux_treeroot (h))
        ret = increment_jobid (h);
    else {
        json_object *na = json_object_new_object ();
        json_object *o = flux_rpc (h, na, "job.next-id");
        if (util_json_object_get_int64 (o, "id", (int64_t *) &ret) < 0) {
            err ("lwj_next_id: Bad object!");
            ret = 0;
        }
        json_object_put (o);
        json_object_put (na);
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

static void add_jobinfo (flux_t h, int64_t id, json_object *req)
{
    char buf [64];
    json_object_iter i;
    json_object *o;
    kvsdir_t dir;

    if (kvs_get_dir (h, &dir, "lwj.%lu", id) < 0)
        err_exit ("kvs_get_dir (id=%lu)", id);

    json_object_object_foreachC (req, i)
        kvsdir_put (dir, i.key, i.val);

    o = json_object_new_string (ctime_iso8601_now (buf, sizeof (buf)));
    kvsdir_put (dir, "create-time", o);
    json_object_put (o);

    kvsdir_destroy (dir);
}

#if SIMULATOR_RACE_WORKAROUND
static int wait_for_lwj_watch_init (flux_t h, int64_t id)
{
    int rc;
    json_object *rpc_o;
    json_object *rpc_resp;

    rpc_o = util_json_object_new_object ();
    util_json_object_add_string (rpc_o, "key", "lwj.next-id");
    util_json_object_add_int64 (rpc_o, "val", id);
    rpc_resp = flux_rpc (h, rpc_o, "sim_sched.lwj-watch");
    util_json_object_get_int (rpc_resp, "rc", &rc);
    json_object_put (rpc_resp);
    json_object_put (rpc_o);
    return rc;
}
#endif

static int job_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    json_object *o = NULL;
    char *tag;

    if (flux_msg_decode (*zmsg, &tag, &o) >= 0) {
        if (strcmp (tag, "job.next-id") == 0) {
            if (flux_treeroot (h)) {
                unsigned long id = lwj_next_id (h);
                json_object *ox = json_id (id);
                flux_respond (h, zmsg, ox);
                json_object_put (o);
            }
            else {
                fprintf (stderr, "%s: forwarding request\n", tag);
                flux_request_send (h, o, tag);
            }
        }
        if (strcmp (tag, "job.create") == 0) {
            json_object *jobinfo = NULL;
            unsigned long id = lwj_next_id (h);
#if SIMULATOR_RACE_WORKAROUND
            //"Fix" for Race Condition
            if (wait_for_lwj_watch_init (h, id) < 0) {
                flux_respond_errnum (h, zmsg, errno);
                goto out;
            }
#endif
            int rc = kvs_job_new (h, id);
            if (rc < 0) {
                flux_respond_errnum (h, zmsg, errno);
                goto out;
            }
            add_jobinfo (h, id, o);

            kvs_commit (h);

            /* Generate reply with new jobid */
            jobinfo = util_json_object_new_object ();
            util_json_object_add_int64 (jobinfo, "jobid", id);
            flux_respond (h, zmsg, jobinfo);
            json_object_put (jobinfo);
        }
    }

out:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
    return 0;
}

int mod_main (flux_t h, zhash_t *args)
{
    if (flux_msghandler_add (h, FLUX_MSGTYPE_REQUEST, "job.*",
                                            job_request_cb, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_add: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("job");

/*
 * vi: ts=4 sw=4 expandtab
 */
