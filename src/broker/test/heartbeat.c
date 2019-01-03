/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>
#include <czmq.h>
#include <stdio.h>

#include "heartbeat.h"

#include "src/common/libtap/tap.h"

void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

void heartbeat_event_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    heartbeat_t *hb = arg;
    int epoch = -1;
    int rc = flux_heartbeat_decode (msg, &epoch);

    ok (rc == 0,
        "received heartbeat event epoch %d", epoch);
    if (epoch == 2) {
        flux_msg_handler_stop (w);
        heartbeat_stop (hb);
    }
}

void check_codec (void)
{
    flux_msg_t *msg;
    int epoch;

    ok ((msg = flux_heartbeat_encode (44000)) != NULL,
        "flux_heartbeat_encode works");
    ok (flux_heartbeat_decode (msg, &epoch) == 0 && epoch == 44000,
        "flux_heartbeat_decode works and returns encoded epoch");
    flux_msg_destroy (msg);
}

int main (int argc, char **argv)
{
    flux_t *h;
    heartbeat_t *hb;
    flux_msg_handler_t *w;

    plan (18);

    check_codec ();

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);

    ok ((hb = heartbeat_create ()) != NULL,
        "heartbeat_create works");

    heartbeat_set_flux (hb, h);

    ok (heartbeat_get_rate (hb) == 2.,
        "heartbeat_get_rate returns default of 2s");
    errno = 0;
    ok (heartbeat_set_rate (hb, -1) < 1 && errno == EINVAL,
        "heartbeat_set_rate -1 fails with EINVAL");
    errno = 0;
    ok (heartbeat_set_rate (hb, 1000000) < 1 && errno == EINVAL,
        "heartbeat_set_rate 1000000 fails with EINVAL");
    ok (heartbeat_set_ratestr (hb, "250ms") == 0,
        "heartbeat_set_ratestr 250ms works");
    ok (heartbeat_get_rate (hb) == 0.250,
        "heartbeat_get_rate returns what was set");
    ok (heartbeat_set_rate (hb, 0.1) == 0,
        "heartbeat_set_rate 0.1 works");
    ok (heartbeat_get_rate (hb) == 0.1,
        "heartbeat_get_rate returns what was set");

    ok (heartbeat_get_epoch (hb) == 0,
        "heartbeat_get_epoch works, default is zero");

    w = flux_msg_handler_create (h, FLUX_MATCH_EVENT, heartbeat_event_cb, hb);
    ok (w != NULL,
        "created event watcher");
    flux_msg_handler_start (w);

    ok (heartbeat_start (hb) == 0,
        "heartbeat_start works");

    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0,
        "flux reactor exited normally");

    heartbeat_destroy (hb);
    flux_msg_handler_destroy (w);
    flux_close (h);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
