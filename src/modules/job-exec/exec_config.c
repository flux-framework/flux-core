/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux bulk-exec configuration code */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <jansson.h>
#include <unistd.h>

#if HAVE_FLUX_SECURITY
#include <flux/security/version.h>
#endif

#include "exec_config.h"
#include "ccan/str/str.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/jpath.h"

static const char *default_cwd = "/tmp";

struct exec_config {
    const char *default_job_shell;
    const char *flux_imp_path;
    const char *exec_service;
    int exec_service_override;
    json_t *sdexec_properties;
    int sdexec_stop_timer_sec;
    int sdexec_stop_timer_signal;
    double default_barrier_timeout;
};

/* Global configs initialized in config_init() */
static struct exec_config exec_conf;

static const char *jobspec_get_job_shell (json_t *jobspec)
{
    const char *path = NULL;
    if (jobspec)
        (void) json_unpack (jobspec,
                            "{s:{s:{s:{s:s}}}}",
                            "attributes",
                             "system",
                              "exec",
                               "job_shell", &path);
    return path;
}

const char *config_get_job_shell (struct jobinfo *job)
{
    const char *path = NULL;
    if (job)
        path = jobspec_get_job_shell (job->jobspec);
    return path ? path : exec_conf.default_job_shell;
}

const char *config_get_imp_path (void)
{
    return exec_conf.flux_imp_path;
}

const char *config_get_exec_service (void)
{
    return exec_conf.exec_service;
}

bool config_get_exec_service_override (void)
{
    return exec_conf.exec_service_override;
}

json_t *config_get_sdexec_properties (void)
{
    return exec_conf.sdexec_properties;
}

const char *config_get_sdexec_stop_timer_sec (void)
{
    static char buf[32];
    snprintf (buf, sizeof (buf), "%d", exec_conf.sdexec_stop_timer_sec);
    return buf;
}

const char *config_get_sdexec_stop_timer_signal  (void)
{
    static char buf[32];
    snprintf (buf, sizeof (buf), "%d", exec_conf.sdexec_stop_timer_signal);
    return buf;
}

double config_get_default_barrier_timeout (void)
{
    return exec_conf.default_barrier_timeout;
}

int config_get_stats (json_t **config_stats)
{
    json_t *o = NULL;

    if (!(o = json_pack ("{s:s? s:s? s:s? s:s? s:i s:f s:i s:i}",
                         "default_cwd", default_cwd,
                         "default_job_shell", exec_conf.default_job_shell,
                         "flux_imp_path", exec_conf.flux_imp_path,
                         "exec_service", exec_conf.exec_service,
                         "exec_service_override",
                         exec_conf.exec_service_override,
                         "default_barrier_timeout",
                         exec_conf.default_barrier_timeout,
                         "sdexec_stop_timer_sec",
                         exec_conf.sdexec_stop_timer_sec,
                         "sdexec_stop_timer_signal",
                         exec_conf.sdexec_stop_timer_signal))) {
        errno = ENOMEM;
        return -1;
    }

    if (exec_conf.sdexec_properties) {
        if (json_object_set (o,
                             "sdexec_properties",
                             exec_conf.sdexec_properties) < 0)
            goto error;
    }

    (void) jpath_clear_null (o);

    (*config_stats) = o;
    return 0;

error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return -1;
}

static void exec_config_init (struct exec_config *ec)
{
    ec->default_job_shell = flux_conf_builtin_get ("shell_path", FLUX_CONF_AUTO);
    ec->flux_imp_path = NULL;
    ec->exec_service = "rexec";
    ec->exec_service_override = 0;
    ec->sdexec_properties = NULL;
    ec->sdexec_stop_timer_sec = 30;
    ec->sdexec_stop_timer_signal = 10; // SIGUSR1
    ec->default_barrier_timeout = 1800.;
}

/*  Initialize configurations for use by job-exec bulk-exec
 *  implementation
 */
int config_setup (flux_t *h,
                  const flux_conf_t *conf,
                  int argc,
                  char **argv,
                  flux_error_t *errp)
{
    struct exec_config tmpconf;
    const char *barrier_timeout = NULL;
    flux_error_t err;

    /* Per trws comment in 97421e88987535260b10d6a19551cea625f26ce4
     *
     * The musl libc loader doesn't actually unload objects on
     * dlclose, so a subsequent dlopen doesn't re-clear globals and
     * similar.
     *
     * So we must re-initialize globals everytime we reload the module.
     */
    exec_config_init (&tmpconf);

    /*  Check configuration for exec.job-shell */
    if (flux_conf_unpack (conf,
                          &err,
                          "{s?{s?s}}",
                          "exec",
                            "job-shell", &tmpconf.default_job_shell) < 0) {
        errprintf (errp,
                   "error reading config value exec.job-shell: %s",
                   err.text);
        return -1;
    }

    /*  Check configuration for exec.imp */
    if (flux_conf_unpack (conf,
                          &err,
                          "{s?{s?s}}",
                          "exec",
                            "imp", &tmpconf.flux_imp_path) < 0) {
        errprintf (errp,
                   "error reading config value exec.imp: %s",
                   err.text);
        return -1;
    }

    /*  Check configuration for exec.service and exec.service-override */
    if (flux_conf_unpack (conf,
                          &err,
                          "{s?{s?s s?b}}",
                          "exec",
                            "service", &tmpconf.exec_service,
                            "service-override",
                              &tmpconf.exec_service_override) < 0) {
        errprintf (errp,
                   "error reading config value exec.service: %s",
                   err.text);
        return -1;
    }

    /*  Check configuration for exec.sdexec-properties */
    if (flux_conf_unpack (conf,
                          &err,
                          "{s?{s?o}}",
                          "exec",
                            "sdexec-properties",
                              &tmpconf.sdexec_properties) < 0) {
        errprintf (errp,
                  "error reading config table exec.sdexec-properties: %s",
                  err.text);
        return -1;
    }
    if (tmpconf.sdexec_properties) {
        const char *k;
        json_t *v;

        if (!json_is_object (tmpconf.sdexec_properties)) {
            errprintf (errp, "exec.sdexec-properties is not a table");
            errno = EINVAL;
            return -1;
        }
        json_object_foreach (tmpconf.sdexec_properties, k, v) {
            if (!json_is_string (v)) {
                errprintf (errp,
                          "exec.sdexec-properties.%s is not a string",
                          k);
                errno = EINVAL;
                return -1;
            }
        }
    }

    /*  Check configuration for exec.stop-timer-* */
    if (flux_conf_unpack (conf,
                          &err,
                          "{s?{s?i s?i}}",
                          "exec",
                            "sdexec-stop-timer-sec",
                              &tmpconf.sdexec_stop_timer_sec,
                            "sdexec-stop-timer-signal",
                              &tmpconf.sdexec_stop_timer_signal) < 0) {
        errprintf (errp,
                   "error reading config values exec.sdexec-stop-timer-sec: %s"
                   " or exec.sdexec-stop-timer-signal",
                   err.text);
        return -1;
    }

    /*  Check configuration for exec.barrier-timeout */
    if (flux_conf_unpack (conf,
                          &err,
                          "{s?{s?s}}",
                          "exec",
                            "barrier-timeout", &barrier_timeout) < 0) {
        errprintf (errp,
                   "error reading config value exec.barrier-timeout: %s",
                   err.text);
        return -1;
    }
    if (barrier_timeout
        && fsd_parse_duration (barrier_timeout,
                               &tmpconf.default_barrier_timeout) < 0) {
        errprintf (errp,
                   "invalid duration '%s' specified for exec.barrier-timeout",
                   barrier_timeout);
        return -1;
    }


    if (argv && argc) {
        /* Finally, override values on cmdline */
        for (int i = 0; i < argc; i++) {
            if (strstarts (argv[i], "job-shell="))
                tmpconf.default_job_shell = argv[i]+10;
            else if (strstarts (argv[i], "imp="))
                tmpconf.flux_imp_path = argv[i]+4;
            else if (strstarts (argv[i], "service="))
                tmpconf.exec_service = argv[i]+8;
        }
    }

    exec_conf = tmpconf;
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
