/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_MUSTACHE_H
#define _SHELL_MUSTACHE_H

#include <stdio.h>

typedef void (*mustache_log_f) (void *arg,
                                const char *file,
                                int line,
                                const char *func,
                                const char *subsys,
                                int level,
                                const char *fmt,
                                va_list args);

/*  Mustache tag callback function prototype. This function is called
 *   for any mustache tag 'name' found in the template by the mustache
 *   template renderer. E.g. if {{foo}} then the tag callback will be
 *   called with `tag == "foo"`. The function should just write the value
 *   of `tag` to the FILE stream `fp` if found.
 *
 * This function should return < 0 on error, in which case the template
 *   renderer will copy the unsubstituted mustache tag into the result.
 */
typedef int (*mustache_tag_f) (FILE *fp, const char *tag, void *arg);

struct mustache_renderer;

/*  Create a mustache renderer, with mustache tags expanded by tag
 *   callback `cb`. (See callback definition above).
 */
struct mustache_renderer *mustache_renderer_create (mustache_tag_f cb);

void mustache_renderer_destroy (struct mustache_renderer *mr);

/*  Set a custom logger for mustache renderer 'mr'.
 */
void mustache_renderer_set_log (struct mustache_renderer *mr,
                                mustache_log_f log,
                                void *log_data);

/*  Render the mustache template 'template' with renderer 'mr'.
 *  The opaque argument 'arg' will be passed to the tag callback(s).
 */
char * mustache_render (struct mustache_renderer *mr,
                        const char *template,
                        void *arg);

#endif /* !_SHELL_MUSTACHE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
