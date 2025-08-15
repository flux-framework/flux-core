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
#include "ccan/str/str.h"

static void info (flux_t *h,
                  flux_msg_handler_t *mh,
                  const flux_msg_t *msg,
                  void *arg)
{
    if (flux_respond (h, msg, flux_aux_get (h, "flux::name")) < 0)
        flux_log_error (h, "error responding to info request");
}

static void panic (flux_t *h,
                  flux_msg_handler_t *mh,
                  const flux_msg_t *msg,
                  void *arg)
{
    flux_log (h, LOG_CRIT, "panic event received: simulating fatal I/O error");
    errno = EIO;
    flux_reactor_stop_error (flux_get_reactor (h));
}


static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "info", info, 0 },
    { FLUX_MSGTYPE_EVENT, "panic", panic, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_msg_handler_t **handlers = NULL;
    int rc;

    for (int i = 0; i < argc; i++) {
        /* Dynamically register a service name if requested.
         */
        if (strstarts (argv[i], "--service=")) {
            const char *service = argv[i] + 10;
            flux_future_t *f;
            if (!(f = flux_service_register (h, service))
                || flux_rpc_get (f, NULL) < 0) {
                flux_log (h, LOG_ERR, "failed to register service %s", service);
                return -1;
            }
            flux_future_destroy (f);
        }
        else if (streq (argv[i], "--init-failure")) {
            flux_log (h, LOG_INFO, "aborting during init per test request");
            errno = EIO;
            return -1;
        }
        else if (strstarts (argv[i], "--attr-is-cached=")) {
            const char *attr = argv[i] + 17;
            const char *name;
            name = flux_attr_cache_first (h);
            while (name) {
                if (streq (attr, name))
                    break;
                name = flux_attr_cache_next (h);
            }
            if (name == NULL) {
                flux_log (h, LOG_ERR, "attr %s is not present in cache", attr);
                errno = ENOENT;
                return -1;
            }
        }
        else if (streq (argv[i], "--config-is-cached")) {
            if (!flux_get_conf (h)) {
                flux_log (h, LOG_ERR, "config object is not cached");
                errno = ENOENT;
                return -1;
            }
        }
    }

    const char *module_name = flux_aux_get (h, "flux::name");
    char panic_topic[256];

    snprintf (panic_topic, sizeof (panic_topic), "%s.panic", module_name);
    if (flux_event_subscribe (h, panic_topic) < 0)
        flux_log_error (h, "error subscribing to %s", panic_topic);
    if (flux_msg_handler_addvec_ex (h, module_name, htab, NULL, &handlers) < 0)
        return -1;
    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
        flux_log_error (h, "flux_reactor_run");
    flux_msg_handler_delvec (handlers);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
