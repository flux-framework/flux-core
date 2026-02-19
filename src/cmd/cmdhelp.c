/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include <glob.h>
#include <string.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <jansson.h>

#include "src/common/libutil/log.h"

#include "cmdhelp.h"

static json_t *command_list_file_read (const char *path)
{
    json_t *o;
    json_error_t error;

    if (!(o = json_load_file (path, 0, &error))) {
        log_msg ("%s::%d: %s", path, error.line, error.text);
        return NULL;
    }
    if (!json_is_array (o)) {
        log_msg ("%s: not a JSON array", path);
        json_decref (o);
        return (NULL);
    }

    return (o);
}

static void command_list_print (FILE *fp, const char *path)
{
    size_t index;
    json_t *o = NULL;
    json_t *entry;

    if (!(o = command_list_file_read (path)))
        return;

    json_array_foreach (o, index, entry) {
        size_t i;
        const char *description;
        json_t *commands;
        json_t *cmd;
        json_error_t error;

        if (json_unpack_ex (entry, &error, 0,
                            "{s:s s:o}",
                            "description", &description,
                            "commands", &commands) < 0) {
            log_msg ("%s:entry %zu: %s", path, index, error.text);
            goto out;
        }
        fprintf (fp, "\n%s\n", description);
        json_array_foreach (commands, i, cmd) {
            const char *name = NULL;
            const char *desc = NULL;
            if (json_unpack_ex (cmd, &error, 0,
                                "{s:s s:s}",
                                "name", &name,
                                "description", &desc) < 0) {
                log_msg ("%s:entry %zu.%zu: %s", path, index, i, error.text);
                goto out;
            }
            fprintf (fp, "   %-18s %s\n", name, desc);
        }
    }
out:
    json_decref (o);
}

static void emit_command_help_from_pattern (FILE *fp, const char *pattern)
{
    int rc;
    size_t i;
    glob_t gl;

    rc = glob (pattern, GLOB_ERR, NULL, &gl);
    switch (rc) {
        case 0:
            break; /* have results, fall-through. */
        case GLOB_ABORTED:
        case GLOB_NOMATCH:
            /* No help.d directory? */
            return;
        default:
            fprintf (stderr, "glob: unknown error %d\n", rc);
            break;
    }

    for (i = 0; i < gl.gl_pathc; i++) {
        const char *file = gl.gl_pathv[i];
        command_list_print (fp, file);
    }
    globfree (&gl);
}

void emit_command_help (const char *plist, FILE *fp)
{
    char *argz = NULL;
    size_t argz_len = 0;
    char *entry = NULL;

    if (argz_create_sep (plist, ':', &argz, &argz_len) != 0)
        return;

    while ((entry = argz_next (argz, argz_len, entry)))
        emit_command_help_from_pattern (fp, entry);

    free (argz);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
