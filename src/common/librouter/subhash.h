/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_SUBHASH_H
#define _ROUTER_SUBHASH_H

// same proto used for both subscribe and unsubscribe
typedef int (*subscribe_f)(const char *topic, void *arg);

struct subhash *subhash_create (void);
void subhash_destroy (struct subhash *sub);

void subhash_set_subscribe (struct subhash *sub, subscribe_f cb, void *arg);
void subhash_set_unsubscribe (struct subhash *sub, subscribe_f cb, void *arg);

bool subhash_topic_match (struct subhash *sh, const char *topic);

int subhash_subscribe (struct subhash *sh, const char *topic);
int subhash_unsubscribe (struct subhash *sh, const char *topic);



#endif /* !_ROUTER_SUBHASH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
