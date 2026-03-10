/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <unistd.h> // environ def
#include <signal.h>
#include <jansson.h>
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libsubprocess/server.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/stdlog.h"

void tap_logger (void *arg,
                 const char *file,
                 int line,
                 const char *func,
                 const char *subsys,
                 int level,
                 const char *fmt,
                 va_list ap)
{
    char buf[4096];
    int len = sizeof (buf);
    if (vsnprintf (buf, len, fmt, ap) >= len) {
        buf[len-1] = '\0';
        buf[len-2] = '+';
    }
    diag ("%s: %s:%d %s(): %s",
          level == LOG_ERR ? "ERR" : "LOG",
          file, line, func, buf);
}

static int test_server (flux_t *h, void *arg)
{
    const char *service_name = arg;
    int rc = -1;
    subprocess_server_t *srv = NULL;

    if (flux_attr_set_cacheonly (h, "rank", "0") < 0) {
        diag ("flux_attr_set_cacheonly failed");
        goto done;
    }
    if (!(srv = subprocess_server_create (h,
                                          service_name,
                                          "smurf",
                                          tap_logger,
                                          NULL))) {
        diag ("subprocess_server_create failed");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        goto done;
    }
    diag ("destroying subprocess server");
    subprocess_server_destroy (srv);
    diag ("server reactor exiting");
    rc = 0;
done:
    return rc;
}

flux_t *rcmdsrv_create (const char *service_name)
{
    flux_t *h;

    // Without this, process may be terminated by SIGPIPE when writing
    // to stdin of subprocess that has terminated
    signal (SIGPIPE, SIG_IGN);

    if (!(h = test_server_create (0, test_server, (char *)service_name)))
        BAIL_OUT ("test_server_create failed");

    return h;
};

#if HAVE_FLUX_SECURITY
static int test_server_secure (flux_t *h, void *arg)
{
    const char *service_name = arg;
    int rc = -1;
    subprocess_server_t *srv = NULL;
    flux_security_t *sec = NULL;

    if (flux_attr_set_cacheonly (h, "rank", "0") < 0) {
        diag ("flux_attr_set_cacheonly failed");
        goto done;
    }
    if (!(srv = subprocess_server_create (h,
                                          service_name,
                                          "smurf",
                                          tap_logger,
                                          NULL))) {
        diag ("subprocess_server_create failed");
        goto done;
    }
    if (!(sec = flux_security_create (0))
        || flux_security_configure (sec, NULL) < 0) {
        diag ("flux_security_configure: %s",
              sec ? flux_security_last_error (sec) : strerror (errno));
        goto done;
    }
    subprocess_server_set_security (srv, sec, true);
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        goto done;
    }
    rc = 0;
done:
    subprocess_server_destroy (srv);
    flux_security_destroy (sec);
    return rc;
}

flux_t *rcmdsrv_create_secure (const char *service_name)
{
    flux_t *h;

    signal (SIGPIPE, SIG_IGN);

    if (!(h = test_server_create (0, test_server_secure, (char *)service_name)))
        BAIL_OUT ("test_server_create failed");

    return h;
}
#endif /* HAVE_FLUX_SECURITY */

// vi: ts=4 sw=4 expandtab
