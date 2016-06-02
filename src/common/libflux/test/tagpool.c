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

    norm_size = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_SIZE);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    ok (avail == norm_size,
        "regular: all tags available (%d/%d)", avail, norm_size);

    for (i = 0; i < 256; i++) {
        tags[i] = tagpool_alloc (t, 0);
        if (tags[i] == FLUX_MATCHTAG_NONE)
            break;
    }
    ok (i == 256,
        "regular: tagpool_alloc worked %d/256 times", i);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    ok (avail == norm_size - 256,
        "regular: pool depleted by 256 (%d)", norm_size - avail);

    duplicates = 0;
    for (j = 0; j < i; j++) {
        for (k = j + 1; k < i; k++)
            if (tags[j] == tags[k])
                duplicates++;
    }
    ok (duplicates == 0,
        "regular: allocated tags contain no duplicates (%d)", duplicates);

    while (--i >= 0)
        tagpool_free (t, tags[i]);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    ok (avail == norm_size,
        "regular: tagpool_free restored all to pool");

    count = 0;
    while (tagpool_alloc (t, 0) != FLUX_MATCHTAG_NONE)
        count++;
    ok (count == norm_size,
        "regular: tagpool_alloc returns FLUX_MATCHTAG_NONE eventually (%d)", count);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_REGULAR_AVAIL);
    ok (avail == 0,
        "regular: pool is exhausted");

    /* Test groups
     */

    grp_size = tagpool_getattr (t, TAGPOOL_ATTR_GROUP_SIZE);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_GROUP_AVAIL);
    ok (avail == grp_size,
        "group: all tags available (%d/%d)", avail, grp_size);

    for (i = 0; i < 256; i++) {
        tags[i] = tagpool_alloc (t, TAGPOOL_FLAG_GROUP);
        if (tags[i] == FLUX_MATCHTAG_NONE)
            break;
    }
    ok (i == 256,
        "group: tagpool_alloc worked 256 times (%d)", i);
    ok (tagpool_getattr (t, TAGPOOL_ATTR_GROUP_AVAIL) == grp_size - 256,
        "group: pool depleted by 256 (%d)", grp_size - avail);

    duplicates = 0;
    for (j = 0; j < i; j++) {
        for (k = j + 1; k < i; k++)
            if (tags[j] == tags[k])
                duplicates++;
    }
    ok (duplicates == 0,
        "group: allocated tags contain no duplicates (%d)", duplicates);

    while (--i >= 0)
        tagpool_free (t, tags[i]);
    avail = tagpool_getattr (t, TAGPOOL_ATTR_GROUP_AVAIL);
    ok (avail == grp_size,
        "group: tagpool_free restored all to pool");

    count = 0;
    while (tagpool_alloc (t, TAGPOOL_FLAG_GROUP) != FLUX_MATCHTAG_NONE)
        count++;
    ok (count == grp_size,
        "group: tagpool_alloc returns FLUX_MATCHTAG_NONE eventually (%d)", count);
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
