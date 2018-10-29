/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/optparse.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif
#include "src/common/libutil/log.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libjob/job.h"
#include "src/common/libutil/read_all.h"

int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_submitbench (optparse_t *p, int argc, char **argv);
int cmd_id (optparse_t *p, int argc, char **argv);
int cmd_purge (optparse_t *p, int argc, char **argv);
int cmd_priority (optparse_t *p, int argc, char **argv);

static struct optparse_option global_opts[] =  {
    OPTPARSE_TABLE_END
};

static struct optparse_option list_opts[] =  {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Limit output to N jobs",
    },
    { .name = "suppress-header", .key = 's', .has_arg = 0,
      .usage = "Suppress printing of header line",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option submitbench_opts[] =  {
    { .name = "repeat", .key = 'r', .has_arg = 1, .arginfo = "N",
      .usage = "Run N instances of jobspec",
    },
    { .name = "fanout", .key = 'f', .has_arg = 1, .arginfo = "N",
      .usage = "Run at most N RPCs in parallel",
    },
    { .name = "priority", .key = 'p', .has_arg = 1, .arginfo = "N",
      .usage = "Set job priority (0-31, default=16)",
    },
#if HAVE_FLUX_SECURITY
    { .name = "reuse-signature", .key = 'R', .has_arg = 0,
      .usage = "Sign jobspec once and reuse the result for multiple RPCs",
    },
    { .name = "security-config", .key = 'c', .has_arg = 1, .arginfo = "pattern",
      .usage = "Use non-default security config glob",
    },
    { .name = "sign-type", .key = 's', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Use non-default mechanism type to sign J",
    },
#endif
    OPTPARSE_TABLE_END
};

static struct optparse_option id_opts[] =  {
    { .name = "from", .key = 'f', .has_arg = 1,
      .arginfo = "dec|kvs-active|words",
      .usage = "Convert jobid from specified form",
    },
    { .name = "to", .key = 't', .has_arg = 1,
      .arginfo = "dec|kvs-active|words",
      .usage = "Convert jobid to specified form",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand subcommands[] = {
    { "list",
      "[OPTIONS]",
      "List active jobs",
      cmd_list,
      0,
      list_opts
    },
    { "priority",
      "[OPTIONS] id priority",
      "Set job priority",
      cmd_priority,
      0,
      NULL,
    },
    { "purge",
      "[OPTIONS] id ...",
      "Remove job(s)",
      cmd_purge,
      0,
      NULL,
    },
    { "submitbench",
      "[OPTIONS] jobspec",
      "Run job(s)",
      cmd_submitbench,
      0,
      submitbench_opts
    },
    { "id",
      "[OPTIONS] [id ...]",
      "Convert jobid(s) to another form",
      cmd_id,
      0,
      id_opts
    },
    OPTPARSE_SUBCMD_END
};

int usage (optparse_t *p, struct optparse_option *o, const char *optarg)
{
    struct optparse_subcommand *s;
    optparse_print_usage (p);
    fprintf (stderr, "\n");
    fprintf (stderr, "Common commands from flux-job:\n");
    s = subcommands;
    while (s->name) {
        fprintf (stderr, "   %-15s %s\n", s->name, s->doc);
        s++;
    }
    exit (1);
}

int main (int argc, char *argv[])
{
    char *cmdusage = "[OPTIONS] COMMAND ARGS";
    optparse_t *p;
    int optindex;
    int exitval;

    log_init ("flux-job");

    p = optparse_create ("flux-job");

    if (optparse_add_option_table (p, global_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table() failed");

    /* Override help option for our own */
    if (optparse_set (p, OPTPARSE_USAGE, cmdusage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (USAGE)");

    /* Override --help callback in favor of our own above */
    if (optparse_set (p, OPTPARSE_OPTION_CB, "help", usage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set() failed");

    /* Don't print internal subcommands, we do it ourselves */
    if (optparse_set (p, OPTPARSE_PRINT_SUBCMDS, 0) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set (PRINT_SUBCMDS)");

    if (optparse_reg_subcommands (p, subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_reg_subcommands");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    if ((argc - optindex == 0)
        || !optparse_get_subcommand (p, argv[optindex])) {
        usage (p, NULL, NULL);
        exit (1);
    }

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

int cmd_priority (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_future_t *f;
    int priority;
    flux_jobid_t id;
    char *endptr;

    if (optindex != argc - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    errno = 0;
    id = strtoull (argv[optindex++], &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        log_err_exit ("error parsing jobid");
    errno = 0;
    priority = strtol (argv[optindex++], &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        log_err_exit ("error parsing priority");

    if (!(f = flux_job_set_priority (h, id, priority)))
        log_err_exit ("flux_job_set_priority");
    if (flux_rpc_get (f, NULL) < 0)
        log_err_exit ("flux_job_set_priority");
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

int cmd_purge (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    int rc = 0;
    int flags = 0;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    while (optindex < argc) {
        char *arg = argv[optindex++];
        char *endptr;
        flux_jobid_t id;
        flux_future_t *f;

        errno = 0;
        id = strtoull (arg, &endptr, 10);
        if (errno != 0)
            log_err_exit ("error parsing jobid: %s", arg);
        if (!(f = flux_job_purge (h, id, flags)))
            log_err_exit ("flux_job_purge");
        if (flux_rpc_get (f, NULL) < 0) {
            const char *errmsg;
            if ((errmsg = flux_future_error_string (f)))
                log_msg_exit ("%s: %s", arg, errmsg);
            else if (errno == ENOENT)
                log_msg ("%s: no such job", arg);
            else
                log_err ("%s", arg);
            rc = -1;
        }
        flux_future_destroy (f);
    }
    flux_close (h);
    return rc;
}

/* convert floating point timestamp (UNIX epoch, UTC) to ISO 8601 string,
 * with second precision
 */
static int iso_timestr (double timestamp, char *buf, size_t size)
{
    time_t sec = timestamp;
    struct tm tm;

    if (!gmtime_r (&sec, &tm))
        return -1;
    if (strftime (buf, size, "%FT%TZ", &tm) == 0)
        return -1;
    return 0;
}

/* convert job status flags to string
 */
const char *flagstr (char *buf, int bufsz, int flags)
{
    (void)snprintf (buf, bufsz, "%s%s%s%s",
                    (flags & FLUX_JOB_RESOURCE_REQUESTED) ? "r" : "",
                    (flags & FLUX_JOB_RESOURCE_ALLOCATED) ? "R" : "",
                    (flags & FLUX_JOB_EXEC_REQUESTED)     ? "x" : "",
                    (flags & FLUX_JOB_EXEC_RUNNING)       ? "X" : "");

    return buf;
}

int cmd_list (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int max_entries = optparse_get_int (p, "count", 0);
    char *attrs = "[\"id\",\"userid\",\"priority\",\"t_submit\",\"flags\"]";
    flux_t *h;
    flux_future_t *f;
    json_t *jobs;
    size_t index;
    json_t *value;

    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_job_list (h, max_entries, attrs)))
        log_err_exit ("flux_job_list");
    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0)
        log_err_exit ("flux_job_list");
    if (!optparse_hasopt (p, "suppress-header"))
        printf ("%s\t\t%s\t%s\t%s\t%s\n",
                "JOBID", "USERID", "PRI", "FLAGS", "T_SUBMIT");
    json_array_foreach (jobs, index, value) {
        flux_jobid_t id;
        int priority;
        uint32_t userid;
        double t_submit;
        char timestr[80];
        int flags;
        char buf[16];
        if (json_unpack (value, "{s:I s:i s:i s:f s:i}",
                                "id", &id,
                                "priority", &priority,
                                "userid", &userid,
                                "t_submit", &t_submit,
                                "flags", &flags) < 0)
            log_msg_exit ("error parsing job data");
        if (iso_timestr (t_submit, timestr, sizeof (timestr)) < 0)
            log_err_exit ("time conversion error");
        printf ("%llu\t%lu\t%d\t%s\t%s\n", (unsigned long long)id,
                                       (unsigned long)userid,
                                       priority,
                                       flagstr (buf, sizeof (buf), flags),
                                       timestr);
    }
    flux_future_destroy (f);
    flux_close (h);

   return (0);
}

struct submitbench_ctx {
    flux_t *h;
#if HAVE_FLUX_SECURITY
    flux_security_t *sec;
    const char *sign_type;
#endif
    int flags;
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;
    int txcount;
    int rxcount;
    int totcount;
    int max_queue_depth;
    optparse_t *p;
    void *jobspec;
    int jobspecsz;
    const char *J;
    int priority;
};

/* Read entire file 'name' ("-" for stdin).  Exit program on error.
 */
size_t read_jobspec (const char *name, void **bufp)
{
    int fd;
    ssize_t size;
    void *buf;

    if (!strcmp (name, "-"))
        fd = STDIN_FILENO;
    else {
        if ((fd = open (name, O_RDONLY)) < 0)
            log_err_exit ("%s", name);
    }
    if ((size = read_all (fd, &buf)) < 0)
        log_err_exit ("%s", name);
    if (fd != STDIN_FILENO)
        (void)close (fd);
    *bufp = buf;
    return size;
}

/* handle RPC response
 * Once all responses are received, stop prep/check watchers
 * so reactor will stop.
 */
void submitbench_continuation (flux_future_t *f, void *arg)
{
    struct submitbench_ctx *ctx = arg;
    flux_jobid_t id;
    const char *errmsg;

    if (flux_job_submit_get_id (f, &id) < 0) {
        if ((errmsg = flux_future_error_string (f)))
            log_msg_exit ("submit: %s", errmsg);
        else if (errno == ENOSYS)
            log_msg_exit ("submit: job-ingest module is not loaded");
        else
            log_err_exit ("submit");
    }
    printf ("%llu\n", (unsigned long long)id);
    flux_future_destroy (f);

    ctx->rxcount++;
}

/* prep - called before event loop would block
 * Prevent loop from blocking if 'check' could send RPCs.
 * Stop the prep/check watchers if RPCs have all been sent,
 * so that, once responses are received, the reactor will exit naturally.
 */
void submitbench_prep (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct submitbench_ctx *ctx = arg;

    if (ctx->txcount == ctx->totcount) {
        flux_watcher_stop (ctx->prep);
        flux_watcher_stop (ctx->check);
    }
    else if ((ctx->txcount - ctx->rxcount) < ctx->max_queue_depth)
        flux_watcher_start (ctx->idle); // keeps loop from blocking
}

/* check - called after event loop unblocks
 * If there are RPCs to send, send one.
 */
void submitbench_check (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct submitbench_ctx *ctx = arg;
    int flags = ctx->flags;

    flux_watcher_stop (ctx->idle);
    if (ctx->txcount < ctx->totcount
                    && (ctx->txcount - ctx->rxcount) < ctx->max_queue_depth) {
        flux_future_t *f;
#if HAVE_FLUX_SECURITY
        if (ctx->sec) {
            if (!ctx->J || !optparse_hasopt (ctx->p, "reuse-signature")) {
                if (!(ctx->J = flux_sign_wrap (ctx->sec, ctx->jobspec,
                                               ctx->jobspecsz,
                                               ctx->sign_type, 0)))
                    log_err_exit ("flux_sign_wrap: %s",
                                  flux_security_last_error (ctx->sec));
            }
            flags |= FLUX_JOB_PRE_SIGNED;
        }
#endif
        if (!(f = flux_job_submit (ctx->h, ctx->J ? ctx->J : ctx->jobspec,
                                   ctx->priority, flags)))
            log_err_exit ("flux_job_submit");
        if (flux_future_then (f, -1., submitbench_continuation, ctx) < 0)
            log_err_exit ("flux_future_then");
        ctx->txcount++;
    }
}

int cmd_submitbench (optparse_t *p, int argc, char **argv)
{
    flux_reactor_t *r;
    int optindex = optparse_option_index (p);
    struct submitbench_ctx ctx;

    memset (&ctx, 0, sizeof (ctx));

    if (optindex != argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
#if HAVE_FLUX_SECURITY
    /* If any non-default security options are specified, create security
     * context so jobspec can be pre-signed before submission.
     */
    if (optparse_hasopt (p, "security-config")
                            || optparse_hasopt (p, "reuse-signature")
                            || optparse_hasopt (p, "sign-type")) {
        const char *sec_config = optparse_get_str (p, "security-config", NULL);
        if (!(ctx.sec = flux_security_create (0)))
            log_err_exit ("security");
        if (flux_security_configure (ctx.sec, sec_config) < 0)
            log_err_exit ("security config %s", flux_security_last_error (ctx.sec));
        ctx.sign_type = optparse_get_str (p, "sign-type", NULL);
    }
#endif
    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    r = flux_get_reactor (ctx.h);
    ctx.p = p;
    ctx.max_queue_depth = optparse_get_int (p, "fanout", 256);
    ctx.totcount = optparse_get_int (p, "repeat", 1);
    ctx.jobspecsz = read_jobspec (argv[optindex++], &ctx.jobspec);
    ctx.priority = optparse_get_int (p, "priority", FLUX_JOB_PRIORITY_DEFAULT);

    /* Prep/check/idle watchers perform flow control, keeping
     * at most ctx.max_queue_depth RPCs outstanding.
     */
    ctx.prep = flux_prepare_watcher_create (r, submitbench_prep, &ctx);
    ctx.check = flux_check_watcher_create (r, submitbench_check, &ctx);
    ctx.idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!ctx.prep || !ctx.check || !ctx.idle)
        log_err_exit ("flux_watcher_create");
    flux_watcher_start (ctx.prep);
    flux_watcher_start (ctx.check);

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");
#if HAVE_FLUX_SECURITY
    flux_security_destroy (ctx.sec); // invalidates ctx.J
#endif
    flux_close (ctx.h);
    free (ctx.jobspec);
    return 0;
}

void id_convert (optparse_t *p, const char *src, char *dst, int dstsz)
{
    const char *from = optparse_get_str (p, "from", "dec");
    const char *to = optparse_get_str (p, "to", "dec");
    flux_jobid_t id;

    /* src to id
     */
    if (!strcmp (from, "dec")) {
        char *endptr;
        errno = 0;
        id = strtoull (src, &endptr, 10);
        if (errno != 0)
            log_err_exit ("%s", src);
        if (*endptr != '\0')
            log_msg_exit ("%s: malformed input", src);
    }
    else if (!strcmp (from, "kvs-active")) {
        if (strncmp (src, "job.active.", 11) != 0)
            log_msg_exit ("%s: missing 'job.active.' prefix", src);
        if (fluid_decode (src + 11, &id, FLUID_STRING_DOTHEX) < 0)
            log_msg_exit ("%s: malformed input", src);
    }
    else if (!strcmp (from, "words")) {
        if (fluid_decode (src, &id, FLUID_STRING_MNEMONIC) < 0)
            log_msg_exit ("%s: malformed input", src);
    }
    else
        log_msg_exit ("Unknown from=%s", from);

    /* id to dst
     */
    if (!strcmp (to, "dec")) {
        snprintf (dst, dstsz, "%llu", (unsigned long long)id);
    }
    else if (!strcmp (to, "kvs-active")) {
        if (snprintf (dst, dstsz, "job.active.") >= dstsz
                || fluid_encode (dst + strlen (dst), dstsz - strlen (dst),
                                 id, FLUID_STRING_DOTHEX) < 0)
            log_msg_exit ("error encoding id");
    }
    else if (!strcmp (to, "words")) {
        if (fluid_encode (dst, dstsz, id, FLUID_STRING_MNEMONIC) < 0)
            log_msg_exit ("error encoding id");
    }
    else
        log_msg_exit ("Unknown to=%s", to);
}

char *trim_string (char *s)
{
    /* trailing */
    int len = strlen (s);
    while (len > 1 && isspace (s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
    /* leading */
    char *p = s;
    while (*p && isspace (*p))
        p++;
    return p;
}

int cmd_id (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    char dst[256];

    if (optindex == argc) {
        char src[256];
        while ((fgets (src, sizeof (src), stdin))) {
            id_convert (p, trim_string (src), dst, sizeof (dst));
            printf ("%s\n", dst);
        }
    }
    else {
        while (optindex < argc) {
            id_convert (p, argv[optindex++], dst, sizeof (dst));
            printf ("%s\n", dst);
        }
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
