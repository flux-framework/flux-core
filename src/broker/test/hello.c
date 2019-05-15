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

#include "hello.h"

#include "src/common/libtap/tap.h"

static flux_t *h;

void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

void hello_cb (hello_t *hello, void *arg)
{
    int *ip = arg;
    (*ip)++;
}

int main (int argc, char **argv)
{
    hello_t *hello;
    uint32_t rank, size;
    int cb_counter = 0;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE),
                  0);
    ok ((h = flux_open ("loop://", 0)) != NULL, "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);

    /* Simulate a single rank session.
     * Since broker attrs are not set, hwm defaults to 1 (self).
     * It will immediately complete so no need to start reactor.
     */
    flux_attr_set_cacheonly (h, "size", "1");
    flux_attr_set_cacheonly (h, "rank", "0");
    ok (flux_get_size (h, &size) == 0 && size == 1, "size == 1");
    ok (flux_get_rank (h, &rank) == 0 && rank == 0, "rank == 0");

    ok ((hello = hello_create ()) != NULL, "hello_create works");
    hello_set_flux (hello, h);
    hello_set_callback (hello, hello_cb, &cb_counter);
    ok (hello_get_count (hello) == 0, "hello_get_count returned 0");
    ok (hello_complete (hello) == 0, "hello_complete returned false");
    ok (hello_start (hello) == 0, "hello_start works");
    ok (cb_counter == 1, "callback was called");
    ok (hello_get_count (hello) == 1, "hello_get_count returned 1");
    ok (hello_complete (hello) != 0, "hello_complete returned true");
    hello_destroy (hello);

    /* Simulate a 2 node session.
     * Same procedure as above except the session will not complete.
     */
    flux_attr_set_cacheonly (h, "size", "3");
    flux_attr_set_cacheonly (h, "rank", "0");
    ok (flux_get_size (h, &size) == 0 && size == 3, "size == 1");
    ok (flux_get_rank (h, &rank) == 0 && rank == 0, "rank == 0");

    cb_counter = 0;
    ok ((hello = hello_create ()) != NULL, "hello_create works");
    hello_set_flux (hello, h);
    hello_set_callback (hello, hello_cb, &cb_counter);
    ok (hello_get_count (hello) == 0, "hello_get_count returned 0");
    ok (hello_complete (hello) == 0, "hello_complete returned false");
    ok (hello_start (hello) == 0, "hello_start works");
    ok (cb_counter == 1, "callback was called once (for self)");
    ok (hello_get_count (hello) == 1, "hello_get_count returned 1");
    ok (hello_complete (hello) == 0, "hello_complete returned false");
    hello_destroy (hello);

    flux_close (h);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
