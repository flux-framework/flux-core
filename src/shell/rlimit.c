/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell rlimit propagations
 *
 * Call setrlimit(2) for any resource limits defined in
 * attributes.system.shell.options.rlimit
 */
#define FLUX_SHELL_PLUGIN_NAME "rlimit"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <signal.h>
#include <sys/resource.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/shell.h>

#include "ccan/str/str.h"

#include "internal.h"
#include "builtins.h"

static int rlimit_name_to_string (const char *name)
{
    if (streq (name, "cpu"))
        return RLIMIT_CPU;
    if (streq (name, "fsize"))
        return RLIMIT_FSIZE;
    if (streq (name, "data"))
        return RLIMIT_DATA;
    if (streq (name, "stack"))
        return RLIMIT_STACK;
    if (streq (name, "core"))
        return RLIMIT_CORE;
    if (streq (name, "nofile") || streq (name, "ofile"))
        return RLIMIT_NOFILE;
    if (streq (name, "as"))
        return RLIMIT_AS;
    if (streq (name, "rss"))
        return RLIMIT_RSS;
    if (streq (name, "nproc"))
        return RLIMIT_NPROC;
    if (streq (name, "memlock"))
        return RLIMIT_MEMLOCK;
#ifdef RLIMIT_MSGQUEUE
    if (streq (name, "msgqueue"))
        return RLIMIT_MSGQUEUE;
#endif
#ifdef RLIMIT_NICE
    if (streq (name, "nice"))
        return RLIMIT_NICE;
#endif
#ifdef RLIMIT_RTPRIO
    if (streq (name, "rtprio"))
        return RLIMIT_RTPRIO;
#endif
#ifdef RLIMIT_RTTIME
    if (streq (name, "rttime"))
        return RLIMIT_RTTIME;
#endif
#ifdef RLIMIT_SIGPENDING
    if (streq (name, "sigpending"))
        return RLIMIT_SIGPENDING;
#endif
    return -1;
}

static int rlimit_init (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    int rc = 0;
    json_t *value;
    const char *key;
    json_t *o = NULL;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!shell)
        return -1;

    switch (flux_shell_getopt_unpack (shell, "rlimit", "o", &o)) {
        case -1:
            return shell_log_errno ("failed to parse rlimit shell option");
        case 0:
            return 0;
        case 1:
            break;
    }
    json_object_foreach (o, key, value) {
        int resource;
        struct rlimit rlim;
        if (!json_is_integer (value)
            || (resource = rlimit_name_to_string (key)) < 0) {
            char *s = json_dumps (value, JSON_ENCODE_ANY);
            shell_log_error ("invalid shell option rlimit.%s%s%s",
                             key,
                             s ? "=" : "",
                             s ? s : "");
            free (s);
            rc = -1;
            continue;
        }
        if (getrlimit (resource, &rlim) < 0) {
            shell_log_errno ("getrlimit %s", key);
            continue;
        }
        rlim.rlim_cur = json_integer_value (value);
        if (rlim.rlim_max != RLIM_INFINITY
            && (rlim.rlim_max < rlim.rlim_cur
                || rlim.rlim_cur == RLIM_INFINITY)) {
            shell_warn ("%s exceeds current max, raising value to hard limit",
                        key);
            rlim.rlim_cur = rlim.rlim_max;
        }
        if (setrlimit (resource, &rlim) < 0)
            shell_log_errno ("setrlimit %s", key);
    }
    return rc;
}

struct shell_builtin builtin_rlimit = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = rlimit_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
