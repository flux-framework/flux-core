/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job raise/kill */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/sigutil.h"
#include "ccan/str/str.h"
#include "common.h"

struct optparse_option raise_opts[] =  {
    { .name = "severity", .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set exception severity [0-7] (default=0)",
    },
    { .name = "type", .key = 't', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Set exception type (default=cancel)",
    },
    { .name = "message", .key = 'm', .has_arg = 1, .arginfo = "NOTE",
      .usage = "Set exception note",
    },
    OPTPARSE_TABLE_END
};

struct optparse_option raiseall_opts[] =  {
    { .name = "severity", .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set exception severity [0-7] (0 is fatal, default=7)",
    },
    { .name = "user", .key = 'u', .has_arg = 1, .arginfo = "USER",
      .usage = "Set target user or 'all' (instance owner only)",
    },
    { .name = "states", .key = 'S', .has_arg = 1, .arginfo = "STATES",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set target job states (default=ACTIVE)",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Confirm the command",
    },
    OPTPARSE_TABLE_END
};

struct optparse_option kill_opts[] = {
    { .name = "signal", .key = 's', .has_arg = 1, .arginfo = "SIG",
      .usage = "Send signal SIG (default SIGTERM)",
    },
    OPTPARSE_TABLE_END
};

struct optparse_option killall_opts[] = {
    { .name = "signal", .key = 's', .has_arg = 1, .arginfo = "SIG",
      .usage = "Send signal SIG (default SIGTERM)",
    },
    { .name = "user", .key = 'u', .has_arg = 1, .arginfo = "USER",
      .usage = "Set target user or 'all' (instance owner only)",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Confirm the command",
    },
    OPTPARSE_TABLE_END
};

struct jobid_arg {
    const char *arg;
    flux_jobid_t id;
};

static void jobid_arg_free (void **item)
{
    if (item) {
        struct jobid_arg *arg = *item;
        free (arg);
        arg = NULL;
    }
}

static struct jobid_arg *jobid_arg_create (const char *s)
{
    flux_jobid_t id;
    struct jobid_arg *arg;

    if (flux_job_id_parse (s, &id) < 0
        || !(arg = calloc (1, sizeof (*arg))))
        return NULL;
    arg->id = id;
    arg->arg = s;

    return arg;
}

/*  Parse a command line containing a list of jobids and optional
 *   message (or note). Stops processing at first argument that
 *   is not a jobid and, if note != NULL, consumes the rest of the
 *   args and returns as a single string in `note`.
 */
static int parse_jobids_and_note (optparse_t *p,
                                  char **argv,
                                  zlistx_t **args,
                                  char **note)
{
    if (!(*args = zlistx_new ()))
        return -1;
    zlistx_set_destructor (*args, jobid_arg_free);

    /*  Convert each argument to a jobid, stopping at the first failure
     */
    while (*argv) {
        struct jobid_arg *arg;
        if (!(arg = jobid_arg_create (*argv))) {
            if (errno == EINVAL)
                break;
            log_err_exit ("Error processing argument %s", *argv);
        }
        if (!zlistx_add_end (*args, arg))
            log_msg_exit ("Unable to add jobid %s to list", *argv);
        argv++;
    }

    if (*argv != NULL) {
        /*  If a note was not expected, then this command takes
         *  only jobids, and a non-jobid argument should raise a
         *  fatal error.
         */
        if (note == NULL)
            log_msg_exit ("invalid jobid: %s", *argv);

        /*  Forward past `--`, which may have been used to force
         *  separation of jobid and message on command line
         */
        if (streq (*argv, "--"))
            argv++;
        *note = parse_arg_message (argv, "message");
    }
    if (note) {
        /*  If a note was expected, also see if --message was used to set
         *  it on the cmdline. It is an error to specify both --message and
         *  the note in free arguments.
         */
        const char *s;
        if (optparse_getopt (p, "message", &s) && *note)
            log_msg_exit ("Do not set note on command line and with --message");
        else if (s && !(*note = strdup (s)))
            log_err_exit ("failed to duplicate --message=%s", s);
    }
    return 0;
}

/*  Check the wait-all future in `f` and print any errors, one per line.
 */
static int wait_all_check (flux_future_t *f, const char *prefix)
{
    int rc;
    if ((rc = flux_future_get (f, NULL)) < 0) {
        const char *child = flux_future_first_child (f);
        while (child) {
            flux_future_t *cf = flux_future_get_child (f, child);
            if (flux_future_get (cf, NULL) < 0)
                log_msg ("%s %s: %s",
                         prefix,
                         child,
                         future_strerror (cf, errno));
            child = flux_future_next_child (f);
        }
    }
    return rc;
}

int cmd_raise (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int severity = optparse_get_int (p, "severity", 0);
    const char *type = optparse_get_str (p, "type", "cancel");
    flux_t *h;
    char *note = NULL;
    flux_future_t *f;
    zlistx_t *args;
    struct jobid_arg *arg;
    int rc = 0;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    parse_jobids_and_note (p, argv + optindex, &args, &note);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_future_wait_all_create ()))
        log_err_exit ("flux_future_wait_all_create");

    /* Always need to set handle for wait_all future: */
    flux_future_set_flux (f, h);

    arg = zlistx_first (args);
    while (arg) {
        flux_future_t *rf = flux_job_raise (h, arg->id, type, severity, note);
        if (!rf || flux_future_push (f, arg->arg, rf) < 0)
            log_err_exit ("flux_job_raise");
        arg = zlistx_next (args);
    }
    rc = wait_all_check (f, "raise");

    zlistx_destroy (&args);
    flux_future_destroy (f);
    flux_close (h);
    free (note);
    return rc;
}

static int raiseall (flux_t *h,
                     int dry_run,
                     uint32_t userid,
                     int state_mask,
                     int severity,
                     const char *type,
                     const char *note,
                     int *errorsp)
{
    flux_future_t *f;
    int count;
    int errors;

    if (!(f = flux_rpc_pack (h,
                             "job-manager.raiseall",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:i s:i s:i s:s s:s}",
                             "dry_run",
                             dry_run,
                             "userid",
                             userid,
                             "states",
                             state_mask,
                             "severity",
                             severity,
                             "type",
                             type,
                             "note",
                             note ? note : "")))
        log_err_exit ("error sending raiseall request");
    if (flux_rpc_get_unpack (f,
                             "{s:i s:i}",
                             "count",
                             &count,
                             "errors",
                             &errors) < 0)
        log_msg_exit ("raiseall: %s", future_strerror (f, errno));
    flux_future_destroy (f);
    if (errorsp)
        *errorsp = errors;
    return count;
}

int cmd_raiseall (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int severity = optparse_get_int (p, "severity", 7);
    const char *type;
    uint32_t userid;
    int state_mask;
    flux_t *h;
    char *note = NULL;
    int count;
    int errors;
    int dry_run = 1;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    type = argv[optindex++];
    if (optindex < argc)
        note = parse_arg_message (argv + optindex, "message");
    if (optparse_hasopt (p, "states")) {
        state_mask = parse_arg_states (p, "states");
        if ((state_mask & FLUX_JOB_STATE_INACTIVE))
            log_msg_exit ("Exceptions cannot be raised on inactive jobs");
    }
    else
        state_mask = FLUX_JOB_STATE_ACTIVE;
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = getuid ();
    if (optparse_hasopt (p, "force"))
        dry_run = 0;
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    count = raiseall (h,
                      dry_run,
                      userid,
                      state_mask,
                      severity,
                      type,
                      note,
                      &errors);
    if (count > 0 && dry_run)
        log_msg ("Command matched %d jobs (-f to confirm)", count);
    else if (count > 0 && !dry_run)
        log_msg ("Raised exception on %d jobs (%d errors)", count, errors);
    else
        log_msg ("Command matched 0 jobs");

    flux_close (h);
    free (note);
    return 0;
}

int cmd_kill (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    int optindex = optparse_option_index (p);
    zlistx_t *args;
    struct jobid_arg *arg;
    const char *s;
    int signum;
    int rc = 0;

    if (argc - optindex < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    parse_jobids_and_note (p, argv + optindex, &args, NULL);

    s = optparse_get_str (p, "signal", "SIGTERM");
    if ((signum = sigutil_signum (s)) < 0)
        log_msg_exit ("kill: Invalid signal %s", s);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_future_wait_all_create ()))
        log_err_exit ("flux_future_wait_all_create");
    flux_future_set_flux (f, h);

    arg = zlistx_first (args);
    while (arg) {
        flux_future_t *rf = flux_job_kill (h, arg->id, signum);
        if (!rf || flux_future_push (f, arg->arg, rf) < 0)
            log_err_exit ("flux_job_kill");
        arg = zlistx_next (args);
    }
    rc = wait_all_check (f, "kill");

    flux_future_destroy (f);
    flux_close (h);
    return rc;
}

int cmd_killall (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    int optindex = optparse_option_index (p);
    const char *s;
    int signum;
    uint32_t userid;
    int count;
    int dry_run = 1;
    int errors;

    if (argc - optindex > 0) {
        optparse_print_usage (p);
        exit (1);
    }
    s = optparse_get_str (p, "signal", "SIGTERM");
    if ((signum = sigutil_signum (s)) < 0)
        log_msg_exit ("killall: Invalid signal %s", s);
    if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = getuid ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "force"))
        dry_run = 0;
    if (!(f = flux_rpc_pack (h,
                             "job-manager.killall",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:i s:i}",
                             "dry_run",
                             dry_run,
                             "userid",
                             userid,
                             "signum",
                             signum)))
        log_err_exit ("error sending killall request");
    if (flux_rpc_get_unpack (f,
                             "{s:i s:i}",
                             "count",
                             &count,
                             "errors",
                             &errors) < 0)
        log_msg_exit ("killall: %s", future_strerror (f, errno));
    flux_future_destroy (f);
    if (count > 0 && dry_run)
        log_msg ("Command matched %d jobs (-f to confirm)", count);
    else if (count > 0 && !dry_run)
        log_msg ("%s %d jobs (%d errors)", strsignal (signum), count, errors);
    else
        log_msg ("Command matched 0 jobs");
    flux_close (h);
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
