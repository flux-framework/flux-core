/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libflux/message.h"
#include "src/common/libflux/tagpool.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    struct tagpool *t;
    uint32_t tags[256];
    uint32_t avail;
    uint32_t norm_size, grp_size;
    int i, j, k, count, duplicates;

    plan (NO_PLAN);

    t = tagpool_create ();
    ok (t != NULL,
        "tagpool_create works");

    /* Test regular
     */
    tags[0] = tagpool_alloc (t, 0);
    ok (tags[0] == 1,
        "regular: allocated first tag");
    tags[1] = tagpool_alloc (t, 0);
    ok (tags[1] == 2,
        "regular: allocated second tag");
    tagpool_free (t, tags[0]);
    tags[2] = tagpool_alloc (t, 0);
    ok (tags[2] == 1,
        "regular: got first tag again after it was freed");
    tagpool_free (t, tags[1]);
    tags[3] = tagpool_alloc (t, 0);
    ok (tags[3] == 2,
        "regular: got second tag again after it was freed");
    tagpool_free (t, tags[2]);
    tagpool_free (t, tags[3]);

    norm_size = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_SIZE);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    ok (avail == norm_size,
        "regular: all tags available");

    ok (avail >= 256,
        "regular: at least 256 tags available");
    for (i = 0; i < 256; i++) {
        tags[i] = tagpool_alloc (t, 0);
        if (tags[i] == FLUX_MATCHTAG_NONE)
            break;
    }
    ok (i == 256,
        "regular: tagpool_alloc worked 256 times");
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    if (avail != norm_size - 256)
        diag ("wrong number avail: %u of %u", avail, norm_size);
    ok (avail == norm_size - 256,
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
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    ok (avail == norm_size,
        "regular: tagpool_free restored all to pool");

    count = 0;
    while (tagpool_alloc (t, 0) != FLUX_MATCHTAG_NONE)
        count++;
    ok (count == norm_size,
        "regular: entire pool allocated by tagpool_alloc loop");
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    ok (avail == 0,
        "regular: pool is exhausted");

    /* Test groups
     */

    grp_size = tagpool_getattr (t, TAGPOOL_ATTR_GROUP_SIZE);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_GROUP_AVAIL);
    ok (avail == grp_size,
        "group: all tags available");

    ok (avail >= 256,
        "regular: at least 256 tags available");
    for (i = 0; i < 256; i++) {
        tags[i] = tagpool_alloc (t, TAGPOOL_FLAG_GROUP);
        if (tags[i] == FLUX_MATCHTAG_NONE)
            break;
    }
    ok (i == 256,
        "group: tagpool_alloc worked 256 times", i);
    ok (tagpool_getattr (t, TAGPOOL_ATTR_GROUP_AVAIL) == grp_size - 256,
        "group: pool depleted by 256");

    duplicates = 0;
    for (j = 0; j < i; j++) {
        for (k = j + 1; k < i; k++)
            if (tags[j] == tags[k])
                duplicates++;
    }
    ok (duplicates == 0,
        "group: allocated tags contain no duplicates");

    while (--i >= 0)
        tagpool_free (t, tags[i]);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_GROUP_AVAIL);
    ok (avail == grp_size,
        "group: tagpool_free restored all to pool");

    count = 0;
    while (tagpool_alloc (t, TAGPOOL_FLAG_GROUP) != FLUX_MATCHTAG_NONE)
        count++;
    ok (count == grp_size,
        "group: entire poool allocated by tagpool_alloc loop");
    avail = tagpool_getattr (t, TAGPOOL_ATTR_GROUP_AVAIL);
    ok (avail == 0,
        "group: pool is exhausted");
    tagpool_destroy (t);

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
