/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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

/* wreck.c - interface with wreck execution system */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <czmq.h>
#include <jansson.h>

#include "wreck.h"

struct state {
    int state;
    const char *str;
};

struct wreck {
    flux_t *h;
    flux_msg_handler_t **handlers;
    zhash_t *active;
    wreck_notify_f notify;
    void *notify_arg;
    int notify_mask;
    int subscribe_mask;
};

static struct state statetab[] = {
    { WRECK_STATE_RESERVED,     "reserved" },
    { WRECK_STATE_SUBMITTED,    "submitted" },
    { WRECK_STATE_RUNNING,      "running" },
    { WRECK_STATE_STARTING,     "starting" },
    { WRECK_STATE_FAILED,       "failed" },
    { WRECK_STATE_COMPLETE,     "complete" },
    { 0, NULL },
};

int wreck_str2state (const char *str)
{
    int i;
    for (i = 0; statetab[i].str != NULL; i++)
        if (!strcmp (str, statetab[i].str))
            return statetab[i].state;
    return -1;
}

const char *wreck_state2str (int state)
{
    int i;
    for (i = 0; statetab[i].str != NULL; i++)
        if (state == statetab[i].state)
            return statetab[i].str;
    return NULL;
}

/* Alter subscriptions to match 'mask'.
 * Return 0 on success, -1 on failure, errno set.
 */
static int wreck_subscribe (struct wreck *wreck, int mask)
{
    int i;
    char s[64];

    for (i = 0; statetab[i].str != NULL; i++) {
        int state = statetab[i].state;

        if ((mask & state)) {
            if (!(wreck->subscribe_mask & state)) {
                snprintf (s, sizeof (s), "wreck.state.%s", statetab[i].str);
                if (flux_event_subscribe (wreck->h, s) < 0)
                    return -1;
                wreck->subscribe_mask |= state;
            }
        }
        else {
            if ((wreck->subscribe_mask & state)) {
                if (flux_event_unsubscribe (wreck->h, s) < 0)
                    return -1;
                wreck->subscribe_mask &= ~state;
            }
        }
    }
    return 0;
}

static void wreck_job_notify (struct wreck_job *job)
{
    struct wreck *wreck = job->wreck;

    if (wreck->notify && (job->state & wreck->notify_mask))
        wreck->notify (job, wreck->notify_arg);
}

int wreck_set_notify (struct wreck *wreck, int notify_mask,
                      wreck_notify_f cb, void *arg)
{
    int submask;
    if (cb == NULL)
        notify_mask = 0;
    submask = notify_mask;
    if (submask != 0) // require terminating states, for hash management
        submask |= WRECK_STATE_COMPLETE | WRECK_STATE_FAILED;
    if (wreck_subscribe (wreck, submask) < 0)
        return -1;

    wreck->notify  = cb;
    wreck->notify_arg = arg;
    wreck->notify_mask = notify_mask;
    return 0;
}

struct wreck_job *wreck_job_incref (struct wreck_job *job)
{
    if (job)
        job->refcount++;
    return job;
}

void wreck_job_decref (struct wreck_job *job)
{
    if (job && --job->refcount == 0) {
        int saved_errno = errno;
        if (job->aux_free && job->aux)
            job->aux_free (job->aux);
        free (job);
        errno = saved_errno;
    }
}

struct wreck_job *wreck_job_lookup (struct wreck *wreck, int64_t id)
{
    struct wreck_job *job;
    char hashkey[16];

    snprintf (hashkey, sizeof (hashkey), "%lld", (long long)id);
    if (!(job = zhash_lookup (wreck->active, hashkey))) {
        errno = ENOENT;
        return NULL;
    }
    return wreck_job_incref (job);
}

static struct wreck_job *wreck_job_create (struct wreck *wreck,
                                           int64_t id, const char *kvs_path,
                                           int state)
{
    struct wreck_job *job;

    if (!(job = calloc (1, sizeof (*job) + strlen (kvs_path) + 1)))
        return NULL;
    job->kvs_path = (char *)(job + 1);
    job->id = id;
    strcpy (job->kvs_path, kvs_path);
    job->wreck = wreck;
    job->state = state;
    job->refcount = 1;
    return job;
}

void wreck_job_aux_set (struct wreck_job *job, void *aux, flux_free_f destroy)
{
    if (job->aux_free && job->aux)
        job->aux_free (job->aux);
    job->aux = aux;
    job->aux_free = destroy;
}

/* Handle a KVS response.  If all outstanding responses have been received,
 * issue state change notification.
 */
static void fetch_job_info_notify_continuation (flux_future_t *f, void *arg)
{
    struct wreck_job *job = arg;
    struct wreck *wreck = job->wreck;
    const char *fq_key = flux_kvs_lookup_get_key (f);
    int key_offset = strlen (job->kvs_path) + 1;
    const char *key = fq_key + key_offset;

    job->fetch_outstanding--;
    if (!fq_key || strlen (fq_key) <= key_offset)
        job->fetch_errors++;
    else if (!strcmp (key, "ntasks")) {
        if (flux_kvs_lookup_get_unpack (f, "i", &job->ntasks) < 0)
            job->fetch_errors++;
    }
    else if (!strcmp (key, "nnodes")) {
        if (flux_kvs_lookup_get_unpack (f, "i", &job->nnodes) < 0)
            job->fetch_errors++;
    }
    else if (!strcmp (key, "walltime")) {
        if (flux_kvs_lookup_get_unpack (f, "I", &job->walltime) < 0)
            job->fetch_errors++;
    }
    if (job->fetch_outstanding == 0) {
        if (job->fetch_errors > 0) {
            flux_log (wreck->h, LOG_ERR, "%s: job=%lld state=%s KVS error",
                      __FUNCTION__, (long long)job->id,
                      wreck_state2str (job->state));
            char hashkey[16];
            snprintf (hashkey, sizeof (hashkey), "%lld", (long long)job->id);
            zhash_delete (wreck->active, hashkey);
        }
        else
            wreck_job_notify (job);
    }
    wreck_job_decref (job);
}

/* Fetch a particular key within the KVS job schema.
 * If the KVS request is successfully sent, responses are handled
 * asynchronously. Once all outstanding responses are received,
 * issue state change notification.
 * Return 0 on success, -1 on failure.
 */
static int fetch_job_info_notify (struct wreck_job *job, const char *key)
{
    struct wreck *wreck = job->wreck;
    flux_future_t *f = NULL;
    char *fq_key = NULL;

    if (asprintf (&fq_key, "%s.%s", job->kvs_path, key) < 0)
        goto error;
    if (!(f = flux_kvs_lookup (wreck->h, 0, fq_key)))
        goto error;
    wreck_job_incref (job);
    if (flux_future_then (f, -1., fetch_job_info_notify_continuation, job) < 0)
        goto error;
    free (fq_key);
    job->fetch_outstanding++;
    return 0;
error:
    job->fetch_errors++;
    free (fq_key);
    flux_future_destroy (f);
    return -1;
}

/* Handle wreck.state.<name> events.
 */
static void wreck_state_cb (flux_t *h, flux_msg_handler_t *mh,
                            const flux_msg_t *msg, void *arg)
{
    struct wreck *wreck = arg;
    int64_t id;
    const char *kvs_path;
    const char *topic;
    char hashkey[16];
    struct wreck_job *job = NULL;
    int state;
    bool newjob = false;

    if (flux_event_unpack (msg, &topic, "{s:I s:s}",
                           "lwj", &id,
                           "kvs_path", &kvs_path) < 0) {
        flux_log_error (h, "%s: decode error", __FUNCTION__);
        return;
    }
    topic += 12; // skip past "wreck.state." (12 chars)
    if ((state = wreck_str2state (topic)) < 0) {
        flux_log (h, LOG_ERR, "%s: job=%lld unknown state=%s",
                  __FUNCTION__, (long long)id, topic);
        return;
    }
    snprintf (hashkey, sizeof (hashkey), "%lld", (long long)id);

    /* Lookup job in wreck->active hash by id.  If not found, add it.
     */
    if (!(job = zhash_lookup (wreck->active, hashkey))) {
        if (!(job = wreck_job_create (wreck, id, kvs_path, state))
                || zhash_insert (wreck->active, hashkey, job) < 0) {
            flux_log_error (h, "%s: job=%lld state=%s create error",
                            __FUNCTION__, (long long)id, topic);
            wreck_job_decref (job);
            return;
        }
        zhash_freefn (wreck->active, hashkey,
                      (zhash_free_fn *)wreck_job_decref);
        newjob = true;
    }
    job->state = state;

    /* Notify the user that job has transitioned.
     * If KVS data needs to be fetched first, kick that off and
     * defer notification to the continuation callback.
     */
    if (newjob) {
        if (fetch_job_info_notify (job, "nnodes") < 0
                    || fetch_job_info_notify (job, "ntasks") < 0
                    || fetch_job_info_notify (job, "walltime") < 0) {
            flux_log (h, LOG_ERR, "%s: job=%lld state=%s KVS error",
                      __FUNCTION__, (long long)id, topic);
            zhash_delete (wreck->active, hashkey);
        }
    }
    else
        wreck_job_notify (job);
}

static void wreck_set_state_continuation (flux_future_t *f, void *arg)
{
    struct wreck_job *job = arg;
    struct wreck *wreck = job->wreck;
    const char *state_str = wreck_state2str (job->state);
    char topic[64];
    flux_msg_t *msg = NULL;

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (wreck->h, "%s: kvs_commit", __FUNCTION__);
        goto done;
    }
    snprintf (topic, sizeof (topic), "wreck.state.%s", state_str);
    if (!(msg = flux_event_encode (topic, NULL))
                            || flux_send (wreck->h, msg, 0) < 0) {
        flux_log_error (wreck->h, "%s: sending event", __FUNCTION__);
        goto done;
    }
done:
    flux_msg_destroy (msg);
    flux_future_destroy (f);
}

int wreck_set_state (struct wreck_job *job, int state)
{
    const char *state_str = wreck_state2str (state);
    flux_kvs_txn_t *txn = NULL;
    char *key;
    flux_future_t *f = NULL;
    int saved_errno;

    if (!job || !state_str) {
        errno = EINVAL;
        return -1;
    }
    if (asprintf (&key, "%s.state", job->kvs_path) < 0)
        return -1;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_pack (txn, 0, key, "s", state_str) < 0)
        goto error;
    if (!(f = flux_kvs_commit (job->wreck->h, 0, txn)))
        goto error;
    if (flux_future_then (f, -1., wreck_set_state_continuation, job) < 0)
        goto error;
    job->state = state;
    flux_kvs_txn_destroy (txn);
    free (key);
    return 0;
error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    free (key);
    errno = saved_errno;
    return -1;
}

static void wreck_launch_continuation (flux_future_t *f, void *arg)
{
    struct wreck_job *job = arg;
    struct wreck *wreck = job->wreck;
    flux_msg_t *msg = NULL;
    bool cleanup = true;
    char topic[64];

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (wreck->h, "%s: kvs_commit", __FUNCTION__);
        goto done;
    }
    snprintf (topic, sizeof (topic), "wrexec.run.%lld", (long long)job->id);
    if (!(msg = flux_event_encode (topic, NULL)))
	    goto done;
    if (flux_send (wreck->h, msg, 0) < 0)
	    goto done;
    cleanup = false;
done:
    flux_msg_destroy (msg);
	flux_future_destroy (f);
	if (cleanup) {
	    /* FIXME: set job to failed state (in KVS)
		 * and destroy job, freeing resources.
		 */
    }
}

int wreck_launch (struct wreck_job *job, const char *resources)
{
    struct wreck *wreck = job->wreck;
    int i, count;
    json_t *res = NULL;
    flux_kvs_txn_t *txn = NULL;
    size_t keysz;
    char *key = NULL;
    int cores = 0;
    int saved_errno;
    flux_future_t *f;

    if (!resources || !(res = json_loads (resources, 0, NULL))
                   || (count = json_array_size (res)) == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    keysz = strlen (job->kvs_path) + 64;
    if (!(key = malloc (keysz)))
        goto error;
    for (i = 0; i < count; i++) {
        json_t *obj;
        int rank;
        int corecount;

        if (!(obj = json_array_get (res, i)))
            goto error;
        if (json_unpack (obj, "{s:i s:i}", "rank", &rank,
                                       "corecount", &corecount) < 0)
            goto error;
        snprintf (key, keysz, "%s.rank.%lld.cores", job->kvs_path,
                  (long long)rank);
        if (flux_kvs_txn_pack (txn, 0, key, "i", corecount) < 0)
            goto error;
        cores += corecount;
    }
    if (cores != job->ntasks) {
        errno = EINVAL;
        goto error;
    }
    if (!(f = flux_kvs_commit (wreck->h, 0, txn)))
        goto error;
    if (flux_future_then (f, -1., wreck_launch_continuation, job) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    json_decref (res);
    flux_kvs_txn_destroy (txn);
    free (key);
    return 0;
error:
    saved_errno = errno;
    json_decref (res);
    flux_kvs_txn_destroy (txn);
    free (key);
    errno = saved_errno;
    return -1;
}

void wreck_destroy (struct wreck *wreck)
{
    if (wreck) {
        int saved_errno = errno;
        zhash_destroy (&wreck->active);
        flux_msg_handler_delvec (wreck->handlers);
        wreck_subscribe (wreck, 0); // unsubscribe to all subscribed
        free (wreck);
        errno = saved_errno;
    }
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,   "wreck.state.*",  wreck_state_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct wreck *wreck_create (flux_t *h)
{
    struct wreck *wreck;

    if (!(wreck = calloc (1, sizeof (*wreck))))
        return NULL;
    wreck->h = h;
    if (!(wreck->active = zhash_new ()))
        goto error;
    if (flux_msg_handler_addvec (h, htab, wreck, &wreck->handlers) < 0)
        goto error;
    return wreck;
error:
    wreck_destroy (wreck);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
