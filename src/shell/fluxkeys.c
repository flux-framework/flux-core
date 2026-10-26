/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* fluxkeys.c - builtin shell plugin publishing flux.* keys in a jobdict
 *
 * At shell.init, create a jobdict named "flux" and populate it with local
 * keys describing the job, which a Flux instance launched as this job
 * obtains from its parent through whichever process manager plugin is
 * active:
 *
 *   taskmap               RFC 34 taskmap of the job
 *   instance-level        instance level of a Flux instance launched as
 *                         this job (parent level + 1)
 *   tbon-interface-hint   parent's tbon.interface-hint attribute
 *   constrained-resources "1" if the job's resources are constrained
 *
 * Keys whose source is unavailable are omitted.  Since jobdict.get resolves
 * a name-qualified key through the named dictionary, a process manager's
 * clients read these as flux.taskmap, flux.instance-level, etc. from the
 * process manager's own dictionary with no further plumbing.
 */
#define FLUX_SHELL_PLUGIN_NAME "fluxkeys"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/taskmap.h>

#include "builtins.h"

#define FLUX_DICT "flux"

/* Call a jobdict topic through the plugin stack with packed IN args.
 * On failure, log the jobdict errmsg and return -1.
 */
static int jobdict_call (flux_shell_t *shell,
                         const char *topic,
                         const char *fmt,
                         ...)
{
    flux_plugin_arg_t *args;
    va_list ap;
    int rc = -1;
    const char *errmsg = "failed";

    if (!(args = flux_plugin_arg_create ()))
        return -1;
    va_start (ap, fmt);
    rc = flux_plugin_arg_vpack (args, FLUX_PLUGIN_ARG_IN, fmt, ap);
    va_end (ap);
    if (rc < 0)
        goto done;
    if ((rc = flux_shell_plugstack_call (shell, topic, args)) < 0) {
        (void)flux_plugin_arg_unpack (args,
                                      FLUX_PLUGIN_ARG_OUT,
                                      "{s:s}",
                                      "errmsg", &errmsg);
        shell_log_error ("%s: %s", topic, errmsg);
    }
done:
    flux_plugin_arg_destroy (args);
    return rc;
}

static int put_key (flux_shell_t *shell, const char *key, const char *val)
{
    return jobdict_call (shell,
                         "jobdict.put",
                         "{s:s s:s s:s s:b}",
                         "name", FLUX_DICT,
                         "key", key,
                         "value", val,
                         "local", 1);
}

static int put_taskmap (flux_shell_t *shell)
{
    const struct taskmap *map = flux_shell_get_taskmap (shell);
    char *val;
    int rc;

    if (!map || !(val = taskmap_encode (map, TASKMAP_ENCODE_WRAPPED)))
        return -1;
    rc = put_key (shell, "taskmap", val);
    free (val);
    return rc;
}

static int put_instance_level (flux_shell_t *shell)
{
    flux_t *h = flux_shell_get_flux (shell);
    const char *level;
    char *endptr;
    long l;
    char val[32];

    if (!h || !(level = flux_attr_get (h, "instance-level")))
        return 0;
    errno = 0;
    l = strtol (level, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || l < 0) {
        shell_log_error ("instance-level attribute '%s' is invalid", level);
        errno = EINVAL;
        return -1;
    }
    snprintf (val, sizeof (val), "%ld", l + 1);
    return put_key (shell, "instance-level", val);
}

static int put_tbon_interface_hint (flux_shell_t *shell)
{
    flux_t *h = flux_shell_get_flux (shell);
    const char *hint;

    if (!h || !(hint = flux_attr_get (h, "tbon.interface-hint")))
        return 0;
    return put_key (shell, "tbon-interface-hint", hint);
}

static int put_constrained_resources (flux_shell_t *shell)
{
    int constrained = 0;

    if (flux_shell_info_unpack (shell,
                                "{s?b}",
                                "constrained_resources", &constrained) < 0)
        return -1;
    if (!constrained)
        return 0;
    return put_key (shell, "constrained-resources", "1");
}

/* shell.init runs after the initrc, so plugins that alter the taskmap
 * have already done so.
 */
static int fluxkeys_init (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (jobdict_call (shell, "jobdict.create", "{s:s}", "name", FLUX_DICT) < 0)
        return -1;
    if (put_taskmap (shell) < 0
        || put_instance_level (shell) < 0
        || put_tbon_interface_hint (shell) < 0
        || put_constrained_resources (shell) < 0) {
        shell_log_errno ("error populating %s jobdict", FLUX_DICT);
        return -1;
    }
    return 0;
}

struct shell_builtin builtin_fluxkeys = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = fluxkeys_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
