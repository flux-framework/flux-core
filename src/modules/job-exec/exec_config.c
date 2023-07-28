/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux job-exec configuration common code */

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

static const char *default_cwd = "/tmp";
static const char *default_job_shell = NULL;
static const char *flux_imp_path = NULL;
static const char *exec_service = "rexec";
static int exec_service_override = 0;
static json_t *sdexec_properties = NULL;

static const char *jobspec_get_job_shell (json_t *jobspec)
{
    const char *path = NULL;
    if (jobspec)
        (void) json_unpack (jobspec, "{s:{s:{s:{s:s}}}}",
                                     "attributes", "system", "exec",
                                     "job_shell", &path);
    return path;
}

const char *config_get_job_shell (struct jobinfo *job)
{
    const char *path = NULL;
    if (job)
        path = jobspec_get_job_shell (job->jobspec);
    return path ? path : default_job_shell;
}

static const char *jobspec_get_cwd (json_t *jobspec)
{
    const char *cwd = NULL;
    if (jobspec)
        (void) json_unpack (jobspec, "{s:{s:{s:s}}}",
                                     "attributes", "system",
                                     "cwd", &cwd);
    return cwd;
}

const char *config_get_cwd (struct jobinfo *job)
{
    const char *cwd = NULL;
    if (job) {
        if (job->multiuser)
            cwd = "/";
        else if (!(cwd = jobspec_get_cwd (job->jobspec)))
            cwd = default_cwd;
    }
    return cwd;
}

const char *config_get_imp_path (void)
{
    return flux_imp_path;
}

const char *config_get_exec_service (void)
{
    return exec_service;
}

bool config_get_exec_service_override (void)
{
    return exec_service_override;
}

json_t *config_get_sdexec_properties (void)
{
    return sdexec_properties;
}

/*  Initialize common configurations for use by job-exec exec modules.
 */
int config_init (flux_t *h, int argc, char **argv)
{
    flux_error_t err;

    /*  Set default job shell path from builtin configuration,
     *   allow override via configuration, then cmdline.
     */
    default_job_shell = flux_conf_builtin_get ("shell_path", FLUX_CONF_AUTO);

    /*  Check configuration for exec.job-shell */
    if (flux_conf_unpack (flux_get_conf (h),
                          &err,
                          "{s?{s?s}}",
                          "exec",
                            "job-shell", &default_job_shell) < 0) {
        flux_log (h, LOG_ERR,
                  "error reading config value exec.job-shell: %s",
                  err.text);
        return -1;
    }

    /*  Check configuration for exec.imp */
    if (flux_conf_unpack (flux_get_conf (h),
                          &err,
                          "{s?{s?s}}",
                          "exec",
                            "imp", &flux_imp_path) < 0) {
        flux_log (h, LOG_ERR,
                  "error reading config value exec.imp: %s",
                  err.text);
        return -1;
    }

    /*  Check configuration for exec.service and exec.service-override */
    if (flux_conf_unpack (flux_get_conf (h),
                          &err,
                          "{s?{s?s s?b}}",
                          "exec",
                            "service", &exec_service,
                            "service-override", &exec_service_override) < 0) {
        flux_log (h, LOG_ERR,
                  "error reading config value exec.service: %s",
                  err.text);
        return -1;
    }

    /*  Check configuration for exec.sdexec-properties */
    if (flux_conf_unpack (flux_get_conf (h),
                          &err,
                          "{s?{s?o}}",
                          "exec",
                            "sdexec-properties", &sdexec_properties) < 0) {
        flux_log (h, LOG_ERR,
                  "error reading config table exec.sdexec-properties: %s",
                  err.text);
        return -1;
    }
    if (sdexec_properties) {
        const char *k;
        json_t *v;

        if (!json_is_object (sdexec_properties)) {
            flux_log (h, LOG_ERR, "exec.sdexec-properties is not a table");
            errno = EINVAL;
            return -1;
        }
        json_object_foreach (sdexec_properties, k, v) {
            if (!json_is_string (v)) {
                flux_log (h, LOG_ERR,
                          "exec.sdexec-properties.%s is not a string",
                          k);
                errno = EINVAL;
                return -1;
            }
        }
    }

    if (argv && argc) {
        /* Finally, override values on cmdline */
        for (int i = 0; i < argc; i++) {
            if (strstarts (argv[i], "job-shell="))
                default_job_shell = argv[i]+10;
            else if (strstarts (argv[i], "imp="))
                flux_imp_path = argv[i]+4;
            else if (strstarts (argv[i], "service="))
                exec_service = argv[i]+8;
        }
    }

    flux_log (h, LOG_DEBUG, "using default shell path %s", default_job_shell);
    if (flux_imp_path) {
        flux_log (h,
                  LOG_DEBUG,
                  "using imp path %s (with helper)",
                  flux_imp_path);
    }
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
