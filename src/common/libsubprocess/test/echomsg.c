/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include "src/common/libutil/log.h"
#include "ccan/str/str.h"

int main (int argc, char **argv)
{
    char *uri;
    flux_t *h;
    flux_error_t error;

    log_init ("test_echomsg");

    if (!(uri = getenv ("TEST_URI"))) {
        fprintf (stderr, "Usage: TEST_URI=... test_echomsg\n");
        return (1);
    }
    if (!(h = flux_open_ex (uri, 0, &error)))
        log_err_exit ("%s", error.text);

    for (;;) {
        flux_msg_t *msg;
        const char *topic;

        if (!(msg = flux_recv (h, FLUX_MATCH_ANY, 0)))
            log_err_exit ("flux_recv");

        if (flux_msg_get_topic (msg, &topic) == 0  && streq (topic, "quit")) {
            flux_msg_decref (msg);
            break;
        }
        if (flux_send (h, msg, 0) < 0)
            log_err_exit ("flux_send");

        flux_msg_decref (msg);
    }

    flux_close (h);

    return (0);
}

// vi:ts=4 sw=4 expandtab
