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

enum {
    DMESG_CLEAR = 1,
    DMESG_FOLLOW = 2,
};

static int dmesg_clear (flux_t *h)
{
    flux_future_t *f;
    int rc = -1;

    if (!(f = flux_rpc (h, "log.clear", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static flux_future_t *dmesg_rpc (flux_t *h, bool follow)
{
    return flux_rpc_pack (h, "log.dmesg", FLUX_NODEID_ANY, FLUX_RPC_STREAMING,
                          "{s:b}", "follow", follow);
}

static int dmesg_rpc_get (flux_future_t *f, flux_log_f fun, void *arg)
{
    const char *buf;
    int rc = -1;

    if (flux_rpc_get (f, &buf) < 0)
        goto done;
    fun (buf, strlen (buf), arg);
    rc = 0;
done:
    return rc;
}

int dmesg (flux_t *h, int flags, flux_log_f fun, void *arg)
{
    int rc = -1;
    bool eof = false;
    bool follow = false;

    if (flags & DMESG_FOLLOW)
        follow = true;
    if (fun) {
        flux_future_t *f;
        if (!(f = dmesg_rpc (h, follow)))
            goto done;
        while (!eof) {
            if (dmesg_rpc_get (f, fun, arg) < 0) {
                if (errno != ENODATA) {
                    flux_future_destroy (f);
                    goto done;
                }
                eof = true;
            }
            flux_future_reset (f);
        }
        flux_future_destroy (f);
    }
    if ((flags & DMESG_CLEAR)) {
        if (dmesg_clear (h) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

void dmesg_fprint (const char *buf, int len, void *arg)
{
    FILE *f = arg;
    struct stdlog_header hdr;
    const char *msg;
    int msglen, severity;
    uint32_t nodeid;

    if (f) {
        if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
            fprintf (f, "%.*s\n", len, buf);
        else {
            nodeid = strtoul (hdr.hostname, NULL, 10);
            severity = STDLOG_SEVERITY (hdr.pri);
            fprintf (f, "%s %s.%s[%" PRIu32 "]: %.*s\n",
                     hdr.timestamp,
                     hdr.appname,
                     stdlog_severity_to_string (severity),
                     nodeid,
                     msglen, msg);
        }
        fflush (f);
    }
}

static int cmd_dmesg (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    int flags = 0;
    flux_log_f print_cb = dmesg_fprint;

    if ((n = optparse_option_index (p)) != ac)
        log_msg_exit ("flux-dmesg accepts no free arguments");

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "read-clear") || optparse_hasopt (p, "clear"))
        flags |= DMESG_CLEAR;
    if (optparse_hasopt (p, "clear"))
        print_cb = NULL;
    if (optparse_hasopt (p, "follow"))
        flags |= DMESG_FOLLOW;
    if (dmesg (h, flags, print_cb, stdout) < 0)
        log_err_exit ("log.dmesg");
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
