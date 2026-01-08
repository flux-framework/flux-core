/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "mustache.h"
#include "src/common/libutil/llog.h"

struct mustache_renderer {
    mustache_tag_f tag_f;
    mustache_log_f llog;
    void *llog_data;
};

void mustache_renderer_destroy (struct mustache_renderer *mr)
{
    free (mr);
}


struct mustache_renderer * mustache_renderer_create (mustache_tag_f tagfn)
{
    struct mustache_renderer *mr = NULL;

    if (!tagfn) {
        errno = EINVAL;
        return NULL;
    }
    if (!(mr = calloc (1, sizeof (*mr))))
        return NULL;
    mr->tag_f = tagfn;
    return (mr);
}

void mustache_renderer_set_log (struct mustache_renderer *mr,
                                mustache_log_f log_f,
                                void *log_arg)
{
    if (mr) {
        mr->llog = log_f;
        mr->llog_data = log_arg;
    }
}

char *mustache_render (struct mustache_renderer *mr,
                       const char *template,
                       void *arg)
{
    char name [1024];
    size_t size;
    char *result = NULL;
    const char *pos = template;
    FILE *fp = NULL;

    if (mr == NULL || template == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (!(fp = open_memstream (&result, &size))) {
        llog_error (mr, "open_memstream");
        goto fail;
    }
    for (;;) {
        int len;
        const char *end;

        /*  Look for opening "{{"
         */
        const char *start = strstr (pos, "{{");
        if (start == NULL) {
            /*  No more mustache tags, put rest of string and finish
             */
            if (fputs (pos, fp) < 0) {
                llog_error (mr, "fputs(%s): %s", pos, strerror (errno));
                goto fail;
            }
            break;
        }
        /*  Write any part of template from current position to next tag
         */
        if (start > pos && fwrite (pos, start - pos, 1, fp) == 0) {
            llog_error (mr, "fwrite: %s", strerror (errno));
            goto fail;
        }
        /*  Advance past opening braces
         */
        start += 2;

        /*  Find end of template tag
         */
        end = strstr (start, "}}");
        if (end == NULL) {
            llog_error (mr, "mustache template error at pos=%d",
                        (int)(start-template));
            /*  Copy rest of template and exit
             */
            pos = start - 2;
            if (fputs (pos, fp) < 0) {
                llog_error (mr, "fputs(%s): %s", pos, strerror (errno));
                goto fail;
            }
            break;
        }
        /*  Copy mustache tag into 'name' and substitute with callback
         */
        len = end - start;
        memcpy (name, start, len);
        name[len] = '\0';
        if ((*mr->tag_f) (fp, name, arg) < 0) {
            /*  If callback fails, just fail to expand the current mustache tag.
             */
            fprintf (fp, "{{%s}}", name);
        }

        /*  Advance past closing braces '}}' and continue processing
         */
        pos = end + 2;
    }
    fclose (fp);
    return result;
fail:
    if (fp) {
        fclose (fp);
        free (result);
    }
    return strdup (template);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
