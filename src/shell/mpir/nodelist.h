/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <jansson.h>

#ifndef HAVE_NODELIST_H
#define HAVE_NODELIST_H

struct nodelist *nodelist_create ();
void nodelist_destroy (struct nodelist *nl);
int nodelist_append (struct nodelist *nl, const char *host);
int nodelist_append_list_destroy (struct nodelist *nl1, struct nodelist *nl2);
json_t *nodelist_to_json (struct nodelist *nl);
struct nodelist *nodelist_from_json (json_t *o);

char *nodelist_first (struct nodelist *nl);
char *nodelist_next (struct nodelist *nl);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
