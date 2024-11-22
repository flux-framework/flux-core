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
#include <unistd.h>

#include "src/common/libutil/stdlog.h"
#include "src/common/libutil/timestamp.h"
#include "src/common/libutil/ansi_color.h"
#include "ccan/str/str.h"

struct dmesg_ctx {
    optparse_t *p;
    unsigned int color:1;
    unsigned int delta:1;
    struct tm last_tm;
    struct timeval last_tv;
};

enum {
    DMESG_COLOR_NAME,
    DMESG_COLOR_TIME,
    DMESG_COLOR_TIMEBREAK,
    DMESG_COLOR_ALERT,
    DMESG_COLOR_EMERG,
    DMESG_COLOR_CRIT,
    DMESG_COLOR_ERR,
    DMESG_COLOR_WARNING,
    DMESG_COLOR_DEBUG,
};

static const char *dmesg_colors[] = {
    [DMESG_COLOR_NAME]          = ANSI_COLOR_YELLOW,
    [DMESG_COLOR_TIME]          = ANSI_COLOR_GREEN,
    [DMESG_COLOR_TIMEBREAK]     = ANSI_COLOR_BOLD ANSI_COLOR_GREEN,
    [DMESG_COLOR_ALERT]         = ANSI_COLOR_REVERSE ANSI_COLOR_RED,
    [DMESG_COLOR_EMERG]         = ANSI_COLOR_REVERSE ANSI_COLOR_RED,
    [DMESG_COLOR_CRIT]          = ANSI_COLOR_BOLD ANSI_COLOR_RED,
    [DMESG_COLOR_ERR]           = ANSI_COLOR_RED,
    [DMESG_COLOR_WARNING]       = ANSI_COLOR_BOLD,
    [DMESG_COLOR_DEBUG]         = ANSI_COLOR_BLUE,
};


static struct optparse_option dmesg_opts[] = {
    { .name = "clear",  .key = 'C',  .has_arg = 0,
      .usage = "Clear the ring buffer", },
    { .name = "read-clear",  .key = 'c',  .has_arg = 0,
      .usage = "Clear the ring buffer contents after printing", },
    { .name = "follow",  .key = 'f',  .has_arg = 0,
      .usage = "Track new entries as are logged", },
    { .name = "new",  .key = 'n',  .has_arg = 0,
      .usage = "Show only new log messages", },
    { .name = "human",  .key = 'H',  .has_arg = 0,
      .usage = "Human readable output", },
    { .name = "delta",  .key = 'd',  .has_arg = 0,
      .usage = "With --human, show timestamp delta between messages", },
    { .name = "color", .key = 'L', .has_arg = 2, .arginfo = "WHEN",
      .flags = OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG,
      .usage = "Colorize output when supported; WHEN can be 'always' "
               "(default if omitted), 'never', or 'auto' (default)." },
    OPTPARSE_TABLE_END,
};

static const char *dmesg_color (struct dmesg_ctx *ctx, int type)
{
    if (ctx->color)
        return dmesg_colors [type];
    return "";
}

static const char *dmesg_color_reset (struct dmesg_ctx *ctx)
{
    if (ctx->color)
        return ANSI_COLOR_RESET;
    return "";
}

static double tv_to_double (struct timeval *tv)
{
    return (tv->tv_sec + (tv->tv_usec/1e6));
}

static const char *months[] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec",
    NULL
};

void print_iso_timestamp (struct dmesg_ctx *ctx, struct stdlog_header *hdr)
{
    struct tm tm;
    struct timeval tv;
    char buf[128];
    char tz[16];
    int len = sizeof (buf);

    /* Fall back to using the hdr timestamp string if
     * - the timestamp fails to parse
     * - getting current timezone offset fails
     * - timestamp_tzoffset() returns "Z" (hdr timestamp already in Zulu time)
     */
    if (timestamp_parse (hdr->timestamp, &tm, &tv) < 0
        || strftime (buf, len, "%Y-%m-%dT%T", &tm) == 0
        || timestamp_tzoffset (&tm, tz, sizeof (tz)) < 0
        || streq (tz, "Z")) {
        printf ("%s%s%s ",
                dmesg_color (ctx, DMESG_COLOR_TIME),
                hdr->timestamp,
                dmesg_color_reset (ctx));
        return;
    }
    printf ("%s%s.%.6lu%s%s ",
            dmesg_color (ctx, DMESG_COLOR_TIME),
            buf,
            (unsigned long)tv.tv_usec,
            tz,
            dmesg_color_reset (ctx));
}

void print_human_timestamp (struct dmesg_ctx *ctx, struct stdlog_header *hdr)
{
    struct tm tm;
    struct timeval tv;
    if (timestamp_parse (hdr->timestamp, &tm, &tv) < 0) {
        printf ("%s[%s]%s ",
                dmesg_color (ctx, DMESG_COLOR_TIME),
                hdr->timestamp,
                dmesg_color_reset (ctx));
    }
    if (tm.tm_year == ctx->last_tm.tm_year
        && tm.tm_mon == ctx->last_tm.tm_mon
        && tm.tm_mday == ctx->last_tm.tm_mday
        && tm.tm_hour == ctx->last_tm.tm_hour
        && tm.tm_min == ctx->last_tm.tm_min) {
        /*  Within same minute, print offset in sec */
        double dt = tv_to_double (&tv) - tv_to_double (&ctx->last_tv);
        printf ("%s[%+11.6f]%s ",
                dmesg_color (ctx, DMESG_COLOR_TIME),
                dt,
                dmesg_color_reset (ctx));
        if (ctx->delta)
            ctx->last_tv = tv;
    }
    else {
        /* New minute, print datetime */
        printf ("%s[%s%02d %02d:%02d]%s ",
                dmesg_color (ctx, DMESG_COLOR_TIMEBREAK),
                months [tm.tm_mon],
                tm.tm_mday,
                tm.tm_hour,
                tm.tm_min,
                dmesg_color_reset (ctx));
        ctx->last_tv = tv;
        ctx->last_tm = tm;
    }
}

static const char *severity_color (struct dmesg_ctx *ctx, int severity)
{
    switch (severity) {
        case LOG_EMERG:
            return dmesg_color (ctx, DMESG_COLOR_EMERG);
        case LOG_ALERT:
            return dmesg_color (ctx, DMESG_COLOR_ALERT);
        case LOG_CRIT:
            return dmesg_color (ctx, DMESG_COLOR_CRIT);
        case LOG_ERR:
            return dmesg_color (ctx, DMESG_COLOR_ERR);
        case LOG_WARNING:
            return dmesg_color (ctx, DMESG_COLOR_WARNING);
        case LOG_NOTICE:
        case LOG_INFO:
            return "";
        case LOG_DEBUG:
            return dmesg_color (ctx, DMESG_COLOR_DEBUG);
    }
    return "";
}

typedef void (*timestamp_print_f) (struct dmesg_ctx *ctx,
                                   struct stdlog_header *hdr);

void dmesg_print (struct dmesg_ctx *ctx,
                  const char *buf,
                  int len,
                  timestamp_print_f timestamp_print)
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
        (*timestamp_print) (ctx, &hdr);
        printf ("%s%s.%s[%" PRIu32 "]%s: %s%.*s%s\n",
                dmesg_color (ctx, DMESG_COLOR_NAME),
                hdr.appname,
                stdlog_severity_to_string (severity),
                nodeid,
                dmesg_color_reset (ctx),
                severity_color (ctx, severity),
                msglen, msg,
                dmesg_color_reset (ctx));
    }
    fflush (stdout);
}

static void dmesg_colors_init (struct dmesg_ctx *ctx)
{
    const char *when;

    if (!(when = optparse_get_str (ctx->p, "color", "auto")))
        when = "always";
    if (streq (when, "always"))
        ctx->color = 1;
    else if (streq (when, "never"))
        ctx->color = 0;
    else if (streq (when, "auto"))
        ctx->color = isatty (STDOUT_FILENO) ? 1 : 0;
    else
        log_msg_exit ("Invalid argument to --color: '%s'", when);
}

static void dmesg_ctx_init (struct dmesg_ctx *ctx, optparse_t *p)
{
    memset (ctx, 0, sizeof (*ctx));
    ctx->p = p;
    dmesg_colors_init (ctx);
    if (optparse_hasopt (p, "delta")) {
        if (!optparse_hasopt (p, "human"))
            log_msg_exit ("--delta can only be used with --human");
        ctx->delta = 1;
    }
}

static int cmd_dmesg (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    struct dmesg_ctx ctx;

    tzset ();

    log_init ("flux-dmesg");
    if ((n = optparse_option_index (p)) != ac)
        log_msg_exit ("flux-dmesg accepts no free arguments");

    dmesg_ctx_init (&ctx, p);

    h = builtin_get_flux_handle (p);

    if (!optparse_hasopt (p, "clear")) {
        flux_future_t *f;
        const char *buf;

        if (!(f = flux_rpc_pack (h,
                                 "log.dmesg",
                                 FLUX_NODEID_ANY,
                                 FLUX_RPC_STREAMING,
                                 "{s:b s:b}",
                                 "follow", optparse_hasopt (p, "follow"),
                                 "nobacklog", optparse_hasopt (p, "new"))))
            log_err_exit ("error sending log.dmesg request");
        while (flux_rpc_get (f, &buf) == 0) {
            timestamp_print_f ts_print = print_iso_timestamp;
            if (optparse_hasopt (p, "human"))
                ts_print = print_human_timestamp;
            dmesg_print (&ctx, buf, strlen (buf), ts_print);
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
