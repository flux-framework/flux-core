/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ZMQUTIL_CERT_H
#define _ZMQUTIL_CERT_H

#include <stdio.h>
#include <stdbool.h>

struct cert *cert_create (void);
struct cert *cert_create_from (const char *public_txt, const char *secret_txt);
void cert_destroy (struct cert *cert);

int cert_write (struct cert *cert, FILE *f);
struct cert *cert_read (FILE *f);

bool cert_equal (struct cert *cert1, struct cert *cert2);

const char *cert_meta_get (struct cert *cert, const char *name);
int cert_meta_set (struct cert *cert, const char *key, const char *val);

const char *cert_public_txt (struct cert *cert);
const char *cert_secret_txt (struct cert *cert);

int cert_apply (struct cert *cert, void *sock);

#endif /* !_ZMQUTIL_CERT_H */

// vi:ts=4 sw=4 expandtab
