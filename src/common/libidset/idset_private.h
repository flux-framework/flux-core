/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_LIBIDSET_PRIVATE_H
#define HAVE_LIBIDSET_PRIVATE_H 1

/* Implemented as a Van Emde Boas tree using code.google.com/p/libveb.
 * T.D is data; T.M is size
 * All ops are O(log m), for key bitsize m: 2^m == T.M.
 */

#include "veb.h"
#include "idset.h"

struct idset {
    size_t count;
    Veb T;
    int flags;
    unsigned int alloc_rr_last;
};

#define IDSET_ENCODE_CHUNK 1024
#define IDSET_DEFAULT_SIZE 1024 // default idset size if size=0

int validate_idset_flags (int flags, int allowed);

int format_first (char *buf,
                  size_t bufsz,
                  const char *fmt,
                  unsigned int id);

#endif /* !HAVE_LIBIDSET_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
