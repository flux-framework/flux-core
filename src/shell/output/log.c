/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell.log output handling
 *
 * The job shell supports plugin subscription to log messages
 * via the shell.log callback topic string. When local stderr
 * is redirected to a file, subscribe to the shell.log callback
 * and decrease the verbosity of the default "eventlogger"
 * logging destination so that log messages go to the configured
 * file instead of the KVS.
 */
#define FLUX_SHELL_PLUGIN_NAME "output.log"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/shell.h>

#include "internal.h"
#include "info.h"
#include "output/output.h"


/*  Level prefix strings. Nominally, output log event 'level' integers
 *   are Internet RFC 5424 severity levels. In the context of flux-shell,
 *   the first 3 levels are equivalently "fatal" errors.
 */
static const char *levelstr[] = {
    "FATAL", "FATAL", "FATAL", "ERROR", " WARN", NULL, "DEBUG", "TRACE"
};

void shell_output_log_file (struct shell_output *out, json_t *context)
{
    const char *msg = NULL;
    const char *file = NULL;
    const char *component = NULL;
    int rank = -1;
    int line = -1;
    int level = -1;
    int fd = out->stderr_fp->fd;
    json_error_t error;

    if (json_unpack_ex (context,
                        &error,
                        0,
                        "{ s?i s:i s:s s?s s?s s?i }",
                        "rank", &rank,
                        "level", &level,
                        "message", &msg,
                        "component", &component,
                        "file", &file,
                        "line", &line) < 0) {
        /*  Ignore log messages that cannot be unpacked so we don't
         *   log an error while logging.
         */
        return;
    }
    dprintf (fd, "flux-shell");
    if (rank >= 0)
        dprintf (fd, "[%d]", rank);
    if (level >= 0 && level <= FLUX_SHELL_TRACE)
        dprintf (fd, ": %s", levelstr [level]);
    if (component)
        dprintf (fd, ": %s", component);
    dprintf (fd, ": %s\n", msg);
}

static int log_output (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct shell_output *out = data;
    int level = -1;
    json_t *context = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i}",
                                "level", &level) < 0)
        return -1;
    if (level > FLUX_SHELL_NOTICE + out->shell->verbose)
        return 0;
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN, "o", &context) < 0)
        return -1;

    return shell_output_write_entry (out, "log", context);
}

void shell_output_log_init (flux_plugin_t *p, struct shell_output *out)
{
    if (out->conf->err.type == FLUX_OUTPUT_TYPE_FILE) {
        if (flux_plugin_add_handler (p, "shell.log", log_output, out) < 0)
            shell_log_errno ("failed to add shell.log handler");
        flux_shell_log_setlevel (FLUX_SHELL_QUIET, "eventlog");
    }
}

/* vi: ts=4 sw=4 expandtab
 */
