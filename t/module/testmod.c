/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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

static void info (flux_t *h,
                  flux_msg_handler_t *mh,
                  const flux_msg_t *msg,
                  void *arg)
{
    if (flux_respond (h, msg, flux_aux_get (h, "flux::name")) < 0)
        flux_log_error (h, "error responding to info request");
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "info", info, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_msg_handler_t **handlers = NULL;
    int rc;

    if (argc == 1 && !strcmp (argv[0], "--init-failure")) {
        flux_log (h, LOG_INFO, "aborting during init per test request");
        errno = EIO;
        return -1;
    }
    if (flux_msg_handler_addvec_ex (h,
                                    flux_aux_get (h, "flux::name"),
                                    htab,
                                    NULL,
                                    &handlers) < 0)
        return -1;
    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
        flux_log_error (h, "flux_reactor_run");
    flux_msg_handler_delvec (handlers);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
