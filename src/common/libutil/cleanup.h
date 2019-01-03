/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

struct cleaner;
typedef void(cleaner_fun_f)(const struct cleaner*c);

void cleanup_directory_recursive (const struct cleaner *c);
void cleanup_directory (const struct cleaner *c);
void cleanup_file (const struct cleaner *c);

void cleanup_push (cleaner_fun_f *fun, void * arg);
void cleanup_push_string (cleaner_fun_f *fun, const char * path);

void cleanup_run (void);
