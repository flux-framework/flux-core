/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Read shell output file options from jobspec:
 *
 * Options of the following form are currently supported:
 *
 * {
 *  "output": {
 *    "mode": "truncate|append",
 *    "stdout" {
 *      "type": "kvs|file",
 *      "path": "template",
 *      "label": true/fals,
 *      "buffer": { "type": "none|line" },
 *    },
 *    "stderr" {
 *      "type": "kvs|file",
 *      "path": "template",
 *      "label": true/false,
 *      "buffer": { "type": "none|line" },
 *   },
 * }
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

/* Note: necessary for shell log functions
 */
#define FLUX_SHELL_PLUGIN_NAME "output.config"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <jansson.h>

#include <flux/shell.h>

#include "ccan/str/str.h"

#include "output/conf.h"

/* Detect if a mustache template is per-shell or per-task by rendering
 * per-rank template on rank 0 and rank 1, then a per-task template using
 * the first task on this rank. If any of these differ, return true,
 * otherwise, false.
 */
static bool template_is_per_shell (flux_shell_t *shell,
                                   const char *template)
{
    bool result = false;
    char *s1 = NULL;
    char *s2 = NULL;
    char *s3 = NULL;
    flux_shell_task_t *task = flux_shell_task_first (shell);

    /* Handle {{tmpdir}} as a special case, since otherwise it will
     * go undetected as a per-shell mustache template:
     */
    if (strstr (template, "{{tmpdir}}"))
        return true;

    /* Note: if shell_size == 1, then flux_shell_rank_mustache_render()
     * for rank 1 will return NULL, so ensure this conditional fails early
     * in that case:
     */
    if (!(s1 = flux_shell_rank_mustache_render (shell, 1, template))
        || !(s2 = flux_shell_rank_mustache_render (shell, 0, template))
        || !(s3 = flux_shell_task_mustache_render (shell, task, template)))
        goto out;

    if (!streq (s1, s2) || !streq (s2, s3) || !streq (s1, s3))
        result = true;
out:
    free (s1);
    free (s2);
    free (s3);
    return result;
}

static int output_stream_getopts (flux_shell_t *shell,
                                  const char *name,
                                  struct output_stream *stream)
{
    const char *type = NULL;

    if (flux_shell_getopt_unpack (shell,
                                  "output",
                                  "{s?s s?{s?s s?s s?b s?{s?s}}}",
                                  "mode", &stream->mode,
                                  name,
                                   "type", &type,
                                   "path", &stream->template,
                                   "label", &stream->label,
                                   "buffer",
                                     "type", &stream->buffer_type) < 0) {
        shell_log_error ("failed to read %s output options", name);
        return -1;
    }
    if (type && streq (type, "kvs")) {
        stream->template = NULL;
        stream->type = FLUX_OUTPUT_TYPE_KVS;
        return 0;
    }
    if (stream->template) {
        stream->type = FLUX_OUTPUT_TYPE_FILE;
        stream->per_shell = template_is_per_shell (shell, stream->template);
    }

    if (strcasecmp (stream->buffer_type, "none") == 0)
        stream->buffer_type = "none";
    else if (strcasecmp (stream->buffer_type, "line") == 0)
        stream->buffer_type = "line";
    else {
        shell_log_error ("invalid buffer type specified: %s",
                         stream->buffer_type);
        stream->buffer_type = "line";
    }
    return 0;
}

void output_config_destroy (struct output_config *conf)
{
    if (conf) {
        int saved_errno = errno;
        free (conf);
        errno = saved_errno;
    }
}

struct output_config *output_config_create (flux_shell_t *shell)
{
    struct output_config *conf;

    if (!(conf = calloc (1, sizeof (*conf))))
        return NULL;

    conf->out.type = FLUX_OUTPUT_TYPE_KVS;
    conf->out.mode = "truncate";
    conf->out.buffer_type = "line";
    if (output_stream_getopts (shell, "stdout", &conf->out) < 0)
        goto error;

    /* stderr defaults except for buffer_type inherit from stdout:
     */
    conf->err = conf->out;
    conf->err.buffer_type = "none";
    if (output_stream_getopts (shell, "stderr", &conf->err) < 0)
        goto error;

    return conf;
error:
    output_config_destroy (conf);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
