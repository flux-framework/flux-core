#include "src/common/libflux/message.h"
#include "src/common/libflux/tagpool.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    tagpool_t t;
    uint32_t tags[256];
    uint32_t avail;

    plan (9);

    /* Test the singleton pool.
     */

    t = tagpool_create ();
    ok (t != NULL,
        "tagpool_create works");
    uint32_t bsize = tagpool_getattr (t, TAGPOOL_ATTR_BLOCKSIZE);
    uint32_t blocks = tagpool_getattr (t, TAGPOOL_ATTR_BLOCKS);
    uint32_t vebsize = tagpool_getattr (t, TAGPOOL_ATTR_SSIZE);

    avail = tagpool_avail (t);
    ok (avail == ~(uint32_t)0 - bsize + vebsize + 1,
        "tagpool_avail returns correct size for empty pool");

    int i;
    for (i = 0; i < 256; i++) {
        tags[i] = tagpool_alloc (t, 1);
        if (tags[i] == FLUX_MATCHTAG_NONE)
            break;
    }
    ok (tagpool_avail (t) == avail - 256,
        "tagpool_alloc works 256 times");

    int duplicates = 0;
    int j, k;
    for (j = 0; j < i; j++) {
        for (k = j + 1; k < i; k++)
            if (tags[j] == tags[k])
                duplicates++;
    }
    ok (duplicates == 0,
        "allocated tags are unique");

    while (--i >= 0)
        tagpool_free (t, tags[i], 1);
    ok (tagpool_avail (t) == avail,
        "tagpool_free works");

    int count = 0;
    while (tagpool_alloc (t, 1) != FLUX_MATCHTAG_NONE)
        count++;
    ok (count == vebsize,
        "tagpool_alloc returns FLUX_MATCHTAG_NONE when singleton pool exhausted");
    tagpool_destroy (t);

    /* Test the block pool
     */

    t = tagpool_create ();
    ok (t != NULL,
        "tagpool_create works");

    ok (tagpool_alloc (t, bsize + 1) == FLUX_MATCHTAG_NONE,
        "tagpool_alloc returns FLUX_MATCHTAG_NONE when block is too big");

    count = 0;
    while (tagpool_alloc (t, 42) != FLUX_MATCHTAG_NONE)
        count++;
    ok (count == blocks,
        "tagpool_alloc returns FLUX_MATCHTAG_NONE when block pool exhausted");

    tagpool_destroy (t);

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
