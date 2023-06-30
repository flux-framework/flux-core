/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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

#include "src/common/libflux/message.h"
#include "src/common/libflux/tagpool.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    struct tagpool *t;
    uint32_t tags[256];
    uint32_t avail;
    uint32_t size;
    int i, j, k, count, duplicates;

    plan (NO_PLAN);

    t = tagpool_create ();
    ok (t != NULL,
        "tagpool_create works");

    tags[0] = tagpool_alloc (t);
    ok (tags[0] == 1,
        "regular: allocated first tag");
    tags[1] = tagpool_alloc (t);
    ok (tags[1] == 2,
        "regular: allocated second tag");
    tagpool_free (t, tags[0]);
    tags[2] = tagpool_alloc (t);
    ok (tags[2] == 1,
        "regular: got first tag again after it was freed");
    tagpool_free (t, tags[1]);
    tags[3] = tagpool_alloc (t);
    ok (tags[3] == 2,
        "regular: got second tag again after it was freed");
    tagpool_free (t, tags[2]);
    tagpool_free (t, tags[3]);

    size = tagpool_getattr (t, TAGPOOL_ATTR_SIZE);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_AVAIL);
    ok (avail == size,
        "regular: all tags available");

    ok (avail >= 256,
        "regular: at least 256 tags available");
    for (i = 0; i < 256; i++) {
        tags[i] = tagpool_alloc (t);
        if (tags[i] == FLUX_MATCHTAG_NONE)
            break;
    }
    ok (i == 256,
        "regular: tagpool_alloc worked 256 times");
    avail = tagpool_getattr (t, TAGPOOL_ATTR_AVAIL);
    if (avail != size - 256)
        diag ("wrong number avail: %u of %u", avail, size);
    ok (avail == size - 256,
        "regular: pool depleted by 256");

    duplicates = 0;
    for (j = 0; j < i; j++) {
        for (k = j + 1; k < i; k++)
            if (tags[j] == tags[k])
                duplicates++;
    }
    ok (duplicates == 0,
        "regular: allocated tags contain no duplicates");

    while (--i >= 0)
        tagpool_free (t, tags[i]);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_AVAIL);
    ok (avail == size,
        "regular: tagpool_free restored all to pool");

    count = 0;
    while (tagpool_alloc (t) != FLUX_MATCHTAG_NONE)
        count++;
    ok (count == size,
        "regular: entire pool allocated by tagpool_alloc loop");
    avail = tagpool_getattr (t, TAGPOOL_ATTR_AVAIL);
    ok (avail == 0,
        "regular: pool is exhausted");

    tagpool_destroy (t);

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
