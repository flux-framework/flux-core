/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include "builtin.h"
#include <inttypes.h>

#include "src/common/libutil/stdlog.h"

static struct optparse_option dmesg_opts[] = {
    { .name = "clear",  .key = 'C',  .has_arg = 0,
      .usage = "Clear the ring buffer", },
    { .name = "read-clear",  .key = 'c',  .has_arg = 0,
      .usage = "Clear the ring buffer contents after printing", },
    { .name = "follow",  .key = 'f',  .has_arg = 0,
      .usage = "Track new entries as are logged", },
    OPTPARSE_TABLE_END,
};

void dmesg_print (const char *buf, int len)
{
    struct stdlog_header hdr;
    const char *msg;
    int msglen, severity;
    uint32_t nodeid;

    if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
        printf ("%.*s\n", len, buf);
    else {
        nodeid = strtoul (hdr.hostname, NULL, 10);
        severity = STDLOG_SEVERITY (hdr.pri);
        printf ("%s %s.%s[%" PRIu32 "]: %.*s\n",
                 hdr.timestamp,
                 hdr.appname,
                 stdlog_severity_to_string (severity),
                 nodeid,
                 msglen, msg);
    }
    fflush (stdout);
}

static int cmd_dmesg (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;

    if ((n = optparse_option_index (p)) != ac)
        log_msg_exit ("flux-dmesg accepts no free arguments");

    h = builtin_get_flux_handle (p);

    if (!optparse_hasopt (p, "clear")) {
        flux_future_t *f;
        const char *buf;

        if (!(f = flux_rpc_pack (h,
                                 "log.dmesg",
                                 FLUX_NODEID_ANY,
                                 FLUX_RPC_STREAMING,
                                 "{s:b}",
                                 "follow", optparse_hasopt (p, "follow"))))
            log_err_exit ("error sending log.dmesg request");
        while (flux_rpc_get (f, &buf) == 0) {
            dmesg_print (buf, strlen (buf));
            flux_future_reset (f);
        }
        if (errno != ENODATA)
            log_msg_exit ("log.dmesg: %s", future_strerror (f, errno));
        flux_future_destroy (f);
    }
    if (optparse_hasopt (p, "read-clear") || optparse_hasopt (p, "clear")) {
        flux_future_t *f;

        if (!(f = flux_rpc (h, "log.clear", NULL, FLUX_NODEID_ANY, 0))
            || flux_future_get (f, NULL) < 0)
            log_msg_exit ("log.clear: %s", future_strerror (f, errno));
        flux_future_destroy (f);
    }

    flux_close (h);
    return (0);
}

int subcommand_dmesg_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "dmesg",
        cmd_dmesg,
        "[OPTIONS...]",
        "Print or control log ring buffer",
        0,
        dmesg_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
