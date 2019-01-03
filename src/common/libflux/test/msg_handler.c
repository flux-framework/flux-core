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

#include "src/common/libflux/msg_handler.h"
#include "src/common/libtap/tap.h"

/* Create a flux handle with no implementation operation callbacks
 * for limited test purposes.
 */
static flux_t *open_fake (void)
{
    static struct flux_handle_ops ops;
    flux_t *h;

    memset (&ops, 0, sizeof (ops));
    if (!(h = flux_handle_create (NULL, &ops, 0)))
        BAIL_OUT ("could not create fake flux_t handle");
    return h;
}

/* NO-OP message handler
 */
static void dummy_msg_handler (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
}

void test_msg_handler_create (flux_t *h)
{
    flux_msg_handler_t *mh;

    /* ensure we can create message handler with dummy flux_t
     */
    mh = flux_msg_handler_create (h, FLUX_MATCH_ANY, dummy_msg_handler, NULL);
    ok (mh != NULL,
        "able to creat fake message handler");
    flux_msg_handler_destroy (mh);

    /* invalid arguments
     */
    errno = 0;
    ok (flux_msg_handler_create (NULL, FLUX_MATCH_ANY,
                                 dummy_msg_handler, NULL) == NULL
        && errno == EINVAL,
        "flux_msg_handler_create h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_handler_create (h, FLUX_MATCH_ANY, NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_msg_handler_create msg_handler=NULL fails with EINVAL");
}

void test_msg_handler_addvec (flux_t *h)
{
    static struct flux_msg_handler_spec tab[] = {
        { FLUX_MSGTYPE_REQUEST,  "sid", dummy_msg_handler, 0 },
        { FLUX_MSGTYPE_REQUEST,  "nancy", dummy_msg_handler, 0 },
        FLUX_MSGHANDLER_TABLE_END,
    };
    flux_msg_handler_t **handlers = NULL;

    /* Ensure bulk message handlers can be created with dummy flux_t
     */
    ok (flux_msg_handler_addvec (h, tab, NULL, &handlers) == 0,
        "able to create fake message handlers in bulk");
    flux_msg_handler_delvec (handlers);

    /* invalid arguments
     */
    errno = 0;
    ok (flux_msg_handler_addvec (NULL, tab, NULL, &handlers) < 0
        && errno == EINVAL,
        "flux_msg_handler_addvec h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_handler_addvec (h, NULL, NULL, &handlers) < 0
        && errno == EINVAL,
        "flux_msg_handler_addvec tab=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_handler_addvec (h, tab, NULL, NULL) < 0
        && errno == EINVAL,
        "flux_msg_handler_addvec hp=NULL fails with EINVAL");
}

void test_misc (flux_t *h)
{
    errno = 0;
    ok (flux_dispatch_requeue (NULL) < 0 && errno == EINVAL,
        "flux_dispatch_requeue h=NULL fails with EINVAL");
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    h = open_fake();

    test_msg_handler_create (h);
    test_msg_handler_addvec (h);
    test_misc (h);

    flux_close (h);

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

