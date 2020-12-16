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
#include <unistd.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/log.h"


void usage (void)
{
    fprintf (stderr,
"Usage: flux-keygen PATH\n"
);
    exit (1);
}

static char * ctime_iso8601_now (char *buf, size_t sz)
{
    struct tm tm;
    time_t now = time (NULL);

    memset (buf, 0, sz);

    if (!localtime_r (&now, &tm))
        return (NULL);
    strftime (buf, sz, "%FT%T", &tm);

    return (buf);
}

int main (int argc, char *argv[])
{
    zcert_t *cert;
    char buf[64];
    char *path = NULL;

    log_init ("flux-keygen");

    if (argc == 1)
        log_msg ("WARNING: add PATH argument to save generated certificate");
    else if (argc == 2 && *argv[1] != '-')
        path = argv[1];
    else
        usage ();

    if (!(cert = zcert_new ()))
        log_msg_exit ("zcert_new: %s", zmq_strerror (errno));

    if (gethostname (buf, sizeof (buf)) < 0)
        log_err_exit ("gethostname");
    zcert_set_meta (cert, "hostname", "%s", buf);
    zcert_set_meta (cert, "name", "%s", buf); // used in overlay logging
    zcert_set_meta (cert, "time", "%s", ctime_iso8601_now (buf, sizeof (buf)));
    zcert_set_meta (cert, "userid", "%d", getuid ());

    if (path && zcert_save_secret (cert, path) < 0)
        log_msg_exit ("zcert_save_secret %s: %s", path, strerror (errno));

    zcert_destroy (&cert);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
