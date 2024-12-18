/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Send a signal to a job at a predefined value for timeleft
 *
 * Set via attributes.system.shell.options.signal, e.g.:
 *  { "timeleft": 123, "signal": 10 }
 *
 * with defaults:
 *  timeleft = 60.
 *  signal = 10
 *
 * If shell.options.signal is not set in jobspec, then no warning
 * signal is sent.
 */
#define FLUX_SHELL_PLUGIN_NAME "signal"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <string.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libutil/fsd.h"
#include "src/common/libutil/sigutil.h"
#include "internal.h"
#include "builtins.h"

struct shell_signal {
    flux_shell_t *shell;
    flux_jobid_t id;
    flux_watcher_t *watcher;
    double timeleft;
    int signum;
};

static void shell_signal_destroy (struct shell_signal *sig)
{
    if (sig) {
        int saved_errno = errno;
        flux_watcher_destroy (sig->watcher);
        free (sig);
        errno = saved_errno;
    }
}

static void kill_cb (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0)
        shell_log_error ("flux_job_kill");
    flux_future_destroy (f);
}

static void shell_signal_cb (flux_reactor_t *r,
                             flux_watcher_t *w,
                             int revents,
                             void *arg)
{
    struct shell_signal *sig = arg;
    flux_future_t *f;

    shell_log ("job will expire in %.1fs, sending %s to job",
               sig->timeleft,
               sigutil_signame (sig->signum));

    if (!(f = flux_job_kill (sig->shell->h, sig->id, sig->signum))
        || flux_future_then (f, -1., kill_cb, sig) < 0)
        shell_log_error ("failed to send %s to job",
                         sigutil_signame (sig->signum));
}

static int set_timeleft_watcher (struct shell_signal *sig)
{
    double expiration;
    double wakeup;
    double remaining;

    if (flux_shell_info_unpack (sig->shell,
                                "{s:{s:{s:F}}}",
                                "R",
                                "execution",
                                "expiration", &expiration) < 0) {
        shell_log_errno ("unable to get job expiration");
        return -1;
    }

    /* Destroy any current watcher in case this function is called due
     * to an update in a job's expiration.
     */
    flux_watcher_destroy (sig->watcher);
    sig->watcher = NULL;

    if (expiration == 0.) {
        shell_log ("job has no expiration, %s will not be sent",
                   sigutil_signame (sig->signum));
        return 0;
    }

    /* Note: the exec system adjusts expiration by the difference between
     * the starttime (the time at which R was created), and when the 'start'
     * event is posted, (after all job shells have started). This shell
     * plugin does *not* do a similar adjustment, so the signal may be
     * delivered early. However, this difference should be at most a few
     * seconds, which should not be critical in real-world scenarios.
     */

    /* wakeup is the absolute time to signal job, remaining time is
     * for informational purposes only. Note that if wakeup < current time,
     * the callback should fire immediately as expected.
     */
    wakeup = expiration - sig->timeleft;
    remaining = wakeup - flux_reactor_time ();
    shell_debug ("Will send %s to job in %.2fs",
                 sigutil_signame (sig->signum),
                 remaining > 0 ? remaining : 0.);

    if (!(sig->watcher = flux_periodic_watcher_create (sig->shell->r,
                                                       wakeup,
                                                       0.,
                                                       NULL,
                                                       shell_signal_cb,
                                                       sig))) {
        shell_log_errno ("flux_periodic_watcher_create");
        return -1;
    }
    flux_watcher_start (sig->watcher);
    return 0;
}

static int parse_timeleft_value (json_t *val, double *dp)
{
    if (json_is_string (val)) {
        if (fsd_parse_duration (json_string_value (val), dp) < 0)
            return -1;
    }
    else if (json_is_number (val)) {
        if ((*dp = json_number_value (val)) < 0)
            return -1;
    }
    return 0;
}

struct shell_signal *shell_signal_create (flux_shell_t *shell)
{
    int rc;
    json_t *val = NULL;
    json_t *opt = NULL;
    json_error_t error;

    struct shell_signal *sig = calloc (1, sizeof (*sig));
    if (!sig)
        return NULL;
    sig->shell = shell;

    /* Default is to send SIGUSR1 with 60s time left
     */
    sig->signum = SIGUSR1;
    sig->timeleft = 60.;

    if (flux_shell_info_unpack (sig->shell,
                                "{s:I}",
                                "jobid", &sig->id) < 0) {
        shell_log_errno ("failed to get jobid");
        goto error;
    }

    if ((rc = flux_shell_getopt_unpack (shell, "signal", "o", &opt)) < 0) {
        shell_log_errno ("unable to get shell `signal' option");
        goto error;
    }

    /*  If no signal option provided or signal=0, then return and do nothing:
     */
    if (rc == 0 || (json_is_integer (opt) && json_integer_value (opt) == 0)) {
        sig->timeleft = -1.;
        return sig;
    }

    /*  O/w, if `opt` is not a json integer, try to unpack optional args
     *  Note: this will fail with an error if `opt` is not a number or
     *   object, or for invalid types in the object (as expected):
     */
    if (!json_is_integer (opt)
        && json_unpack_ex (opt, &error, 0,
                           "{s?i s?o}",
                           "signum", &sig->signum,
                           "timeleft", &val) < 0) {
        shell_log_error ("error in shell `signal' option: %s", error.text);
        goto error;
    }

    if (val && parse_timeleft_value (val, &sig->timeleft) < 0) {
        shell_log_error ("signal.timeleft=%s is invalid",
                         json_string_value (val));
        goto error;
    }

    return sig;
error:
    shell_signal_destroy (sig);
    return NULL;
}

static int resource_update_cb (flux_plugin_t *p,
                               const char *topic,
                               flux_plugin_arg_t *args,
                               void *arg)
{
    struct shell_signal *sig = arg;
    return set_timeleft_watcher (sig);
}

static int signal_init (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    struct shell_signal *sig;
    int rank;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!shell
        || flux_shell_info_unpack (shell, "{s:i}", "rank", &rank) < 0) {
        shell_log_error ("flux_shell_info_unpack");
        return -1;
    }

    /* signal plugin only operates on rank 0
     */
    if (rank != 0)
        return 0;

    if (!(sig = shell_signal_create (shell)))
        return -1;
    if (flux_plugin_aux_set (p,
                             "signal",
                             sig,
                             (flux_free_f) shell_signal_destroy) < 0) {
        shell_signal_destroy (sig);
        return -1;
    }
    if (sig->timeleft <= 0)
        return 0;

    if (set_timeleft_watcher (sig) < 0)
        return -1;

    if (flux_plugin_add_handler (p,
                                 "shell.resource-update",
                                 resource_update_cb,
                                 sig) < 0)
        shell_log_errno ("unable to subscribe to shell resource updates");

    return 0;
}

struct shell_builtin builtin_signal = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = signal_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
