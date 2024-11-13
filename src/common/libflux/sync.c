/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sync.c - future synchronized with heartbeat
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

struct flux_sync {
    flux_t *h;
    uint32_t seq;
    int count;
    double last;
    double minimum;
};

static const char *auxkey = "flux::sync";

static void sync_destroy (struct flux_sync *sync)
{
    if (sync) {
        int saved_errno = errno;
        flux_future_t *f;
        f = flux_event_unsubscribe_ex (sync->h,
                                       "heartbeat.pulse",
                                       FLUX_RPC_NORESPONSE);
        flux_future_destroy (f);
        free (sync);
        errno = saved_errno;
    }
}

static struct flux_sync *sync_create (flux_t *h, double minimum)
{
    struct flux_sync *sync;
    flux_future_t *f;

    if (!(sync = calloc (1, sizeof (*sync))))
        return NULL;
    sync->h = h;
    sync->minimum = minimum;
    if (!(f = flux_event_subscribe_ex (sync->h,
                                       "heartbeat.pulse",
                                       FLUX_RPC_NORESPONSE)))
        goto error;
    flux_future_destroy (f);
    return sync;
error:
    sync_destroy (sync);
    return NULL;
}

/* Special note about the non-reactive case:  events are delivered to all
 * matching message handlers, not first match like requests.  Therefore, the
 * future implementation must requeue events even if they were matched in the
 * temporary reactor, in case another matching handler exists in the main
 * reactor.  Thus, calling flux_future_get() in a loop on the sync object,
 * where the main reactor's dispatcher doesn't retire the event, would fulfill
 * the future using the same message over and over unless we take steps to
 * prevent that by watching the event sequence number.
 */
static void heartbeat_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    double now = flux_reactor_now (flux_get_reactor (h));
    flux_future_t *f = arg;
    struct flux_sync *sync = flux_future_aux_get (f, auxkey);
    uint32_t seq;

    if (flux_msg_authorize (msg, FLUX_USERID_UNKNOWN) < 0
        || flux_event_decode (msg, NULL, NULL) < 0
        || flux_msg_get_seq (msg, &seq) < 0)
        return;
    if (sync->count > 0) {
        if (seq <= sync->seq)
            return;
        if (sync->minimum > 0. && now - sync->last < sync->minimum)
            return;
    }
    sync->seq = seq;
    sync->count++;
    sync->last = now;
    flux_future_fulfill (f, NULL, NULL);
}

/* Initialize callback is called at most once per reactor context.
 * A message handler must be installed in the reactor context that is being
 * initialized.  Store it in the aux hash so it is destroyed with the future.
 * Use a NULL key since there may be two of them and we don't want one to
 * clobber the other.
 */
static void initialize_cb (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    flux_msg_handler_t *mh;
    struct flux_match match = FLUX_MATCH_EVENT;

    match.topic_glob = "heartbeat.pulse";
    if (!(mh = flux_msg_handler_create (h, match, heartbeat_cb, f)))
        goto error;
    if (flux_future_aux_set (f,
                             NULL,
                             mh,
                             (flux_free_f)flux_msg_handler_destroy) < 0) {
        flux_msg_handler_destroy (mh);
        goto error;
    }
    flux_msg_handler_start (mh);
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}

flux_future_t *flux_sync_create (flux_t *h, double minimum)
{
    flux_future_t *f;
    struct flux_sync *sync;

    if (h == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_future_create (initialize_cb, NULL)))
        return NULL;
    flux_future_set_flux (f, h);
    if (!(sync = sync_create (h, minimum)))
        goto error;
    if (flux_future_aux_set (f, auxkey, sync, (flux_free_f)sync_destroy) < 0) {
        sync_destroy (sync);
        goto error;
    }
    return f;
error:
    flux_future_destroy (f);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
