#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include "src/common/libutil/monotime.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libtap/tap.h"


int main (int argc, char *argv[])
{
    nodeset_t *n, *n2;
    nodeset_iterator_t *itr;
    int i;
    struct timespec ts;
    uint32_t bigset = 1E6;
    char *tmp;

    plan (NO_PLAN);

    n = nodeset_create ();
    ok (n != NULL);
    nodeset_config_brackets (n, false);

    /* obtain constants used in other tests */
    uint32_t maxrank = nodeset_getattr (n, NODESET_ATTR_MAXRANK);
    uint32_t minsize = nodeset_getattr (n, NODESET_ATTR_MINSIZE);
    uint32_t maxsize = nodeset_getattr (n, NODESET_ATTR_MAXSIZE);

    nodeset_add_rank (n, 8);
    nodeset_add_rank (n, 7);
    nodeset_add_rank (n, 9);
    like (nodeset_string (n), "7-9", "consecutive adds become range");
    ok (nodeset_count (n) == 3);

    nodeset_add_rank (n, 1);
    like (nodeset_string (n), "1,7-9", "singleton prepended to range");
    ok (nodeset_count (n) == 4);

    nodeset_add_rank (n, 16);
    like (nodeset_string (n), "1,7-9,16", "singleton appended to range");
    ok (nodeset_count (n) == 5);

    nodeset_add_rank (n, 14);
    like (nodeset_string (n), "1,7-9,14,16", "singleton embedded in range");
    ok (nodeset_count (n) == 6);

    nodeset_add_rank (n, 3);
    like (nodeset_string (n), "1,3,7-9,14,16", "singleton embedded in range 2");
    ok (nodeset_count (n) == 7);

    nodeset_add_range (n, 1, 3);
    like (nodeset_string (n), "1-3,7-9,14,16", "overlapping range");
    ok (nodeset_count (n) == 8);

    nodeset_add_range (n, 5, 8);
    like (nodeset_string (n), "1-3,5-9,14,16", "overlapping range 2");
    ok (nodeset_count (n) == 10);

    nodeset_add_range (n, 8, 11);
    like (nodeset_string (n), "1-3,5-11,14,16", "overlapping range 3");
    ok (nodeset_count (n) == 12);

    nodeset_add_range (n, 1, 16);
    like (nodeset_string (n), "1-16", "add range that contains existing");
    ok (nodeset_count (n) == 16);

    nodeset_add_range (n, 4, 8);
    like (nodeset_string (n), "1-16", "add range contained by existing");
    ok (nodeset_count (n) == 16);

    nodeset_destroy (n);

/********************************************/

    n = nodeset_create ();
    ok (n != NULL);
    nodeset_add_rank (n, 0);
    nodeset_add_rank (n, 1);
    nodeset_add_rank (n, 2);
    like (nodeset_string (n), "\\[0-2\\]", "edge case 1 merges with 0");
    ok (nodeset_count (n) == 3);
    nodeset_config_ranges (n, false);
    like (nodeset_string (n), "\\[0,1,2\\]");
    nodeset_destroy (n);

/********************************************/

    n = nodeset_create ();
    ok (n != NULL);
    nodeset_add_rank (n, 2);
    nodeset_add_rank (n, 1);
    nodeset_add_rank (n, 0);
    like (nodeset_string (n), "\\[0-2\\]", "reverse merge works");
    ok (nodeset_count (n) == 3);
    nodeset_destroy (n);

/********************************************/

    n = nodeset_create_string ("[1,3,5,6-100]");
    ok (n != NULL);
    like (nodeset_string (n), "\\[1,3,5-100\\]", "mundane range string works");
    ok (nodeset_count (n) == 98);
    nodeset_destroy (n);

    n = nodeset_create_string ("2-1");
    ok (n != NULL);
    like (nodeset_string (n), "\\[1-2\\]", "numerically reversed range handled");
    ok (nodeset_count (n) == 2);
    nodeset_destroy (n);

    n = nodeset_create_string ("");
    ok (n != NULL);
    ok (nodeset_count (n) == 0);
    like (nodeset_string (n), "", "empty string produces empty range");
    nodeset_destroy (n);

    n = nodeset_create_string (",");
    ok (n == NULL, "comma by itself produces error");

    n = nodeset_create_string ("-1");
    ok (n == NULL, "range missing start produces error");

    n = nodeset_create_string ("1-");
    ok (n == NULL, "range missing end produces error");

    n = nodeset_create_string ("foo1");
    ok (n == NULL, "alpha with numerical suffix produces error");

    n = nodeset_create_string ("[1-2]");
    ok (n != NULL);
    like (nodeset_string (n), "\\[1-2\\]", "bracketed range works");
    ok (nodeset_count (n) == 2);
    nodeset_destroy (n);

    n = nodeset_create_string ("xyz");
    ok (n == NULL, "alpha by itself produces error");

/********************************************/

    n = nodeset_create_string ("0-2");
    ok (n != NULL);
    ok (nodeset_test_rank (n, 0));
    ok (nodeset_test_rank (n, 1));
    ok (nodeset_test_rank (n, 2));
    ok (!nodeset_test_rank (n, 3));

    ok (!nodeset_test_rank (n, nodeset_getattr (n, NODESET_ATTR_SIZE) - 1),
        "nodeset_test_rank (internal size - 1) fails");
    ok (!nodeset_test_rank (n, nodeset_getattr (n, NODESET_ATTR_SIZE)),
        "nodeset_test_rank (internal size) fails");
    ok (!nodeset_test_rank (n, nodeset_getattr (n, NODESET_ATTR_SIZE) + 1),
        "nodeset_test_rank (internal size + 1) fails");

    ok (!nodeset_test_range (n, 2, nodeset_getattr (n, NODESET_ATTR_SIZE) - 1),
        "nodeset_test_range (2, internal size - 1) fails");
    ok (!nodeset_test_range (n, 2, nodeset_getattr (n, NODESET_ATTR_SIZE)),
        "nodeset_test_range (2, internal size) fails");
    ok (!nodeset_test_range (n, 2, nodeset_getattr (n, NODESET_ATTR_SIZE) + 1),
        "nodeset_test_range (2, internal size + 1) fails");

    ok (!nodeset_test_range (n, nodeset_getattr (n, NODESET_ATTR_SIZE) - 1, 2),
        "nodeset_test_range (internal size - 1, 2) fails");
    ok (!nodeset_test_range (n, nodeset_getattr (n, NODESET_ATTR_SIZE), 2),
        "nodeset_test_range (internal size, 2) fails");
    ok (!nodeset_test_range (n, nodeset_getattr (n, NODESET_ATTR_SIZE) + 1, 2),
        "nodeset_test_range (internal size + 1, 2) fails");

    nodeset_config_brackets (n, false);
    like (nodeset_string (n), "0-2");
    ok (nodeset_test_range (n, 0, 2), "nodeset_test_range works");
    nodeset_delete_rank (n, 0);
    like (nodeset_string (n), "1-2", "nodeset_delete_rank works");
    ok (!nodeset_test_rank (n, 0), "nodeset_test_rank works");
    ok (nodeset_test_range (n, 1, 2));
    nodeset_delete_rank (n, 1);
    ok (!nodeset_test_rank (n, 0));
    ok (!nodeset_test_rank (n, 1));
    ok (nodeset_test_rank (n, 2));
    ok (!strcmp (nodeset_string (n), "2"));
    nodeset_delete_rank (n, 2);
    ok (!nodeset_test_rank (n, 0));
    ok (!nodeset_test_rank (n, 1));
    ok (!nodeset_test_rank (n, 2));
    like (nodeset_string (n), "");
    nodeset_destroy (n);

/********************************************/

    /* Exercise iteration
     */
    n = nodeset_create_string ("0-2");
    ok (n != NULL);
    itr = nodeset_iterator_create (n);
    ok (nodeset_next (itr) == 0, "iterator_next works on first element");
    ok (nodeset_next (itr) == 1, "iterator_next works on next element");
    ok (nodeset_next (itr) == 2, "iterator_next works on last element");
    ok (nodeset_next (itr) == NODESET_EOF, "iterator_next returns EOF");
    nodeset_iterator_rewind (itr);
    ok (nodeset_next (itr) == 0, "iterator rewind works");
    nodeset_iterator_destroy (itr);
    nodeset_destroy (n);

/********************************************/
    /* Exercise iteration with nodeset_next_rank
     */
    n = nodeset_create_string ("0,2-3,7");
    ok (n != NULL);
    int r = nodeset_min (n);
    ok (r == 0, "nodeset_min");
    ok ((r = nodeset_next_rank (n, r)) == 2,
        "nodeset_next_rank (n, min) returns second element");
    ok ((r = nodeset_next_rank (n, r)) == 3,
        "nodeset_next_rank works on third element");
    ok ((r = nodeset_next_rank (n, r)) == 7,
        "nodeset_next_rank works on fourth element");
    ok ((r = nodeset_next_rank (n, r)) == NODESET_EOF,
        "nodeset_next_rank detects end of nodeset");

    ok ((r = nodeset_next_rank (n, 1)) == 2,
        "nodeset_next_rank returns next rank even if arg not in set");
    nodeset_destroy (n);

/********************************************/

    /* Exercise nodeset_dup
     */
    n = nodeset_create_string ("0-2");
    ok (n != NULL);
    nodeset_config_brackets (n, false);
    like (nodeset_string (n), "0-2");
    n2 = nodeset_dup (n);
    ok (n2 != NULL, "nodeset_dup says it worked");
    like (nodeset_string (n2), "0-2", "nodeset_dup returned identical nodeset");
    nodeset_add_rank (n, 4);
    nodeset_add_rank (n2, 5);
    like (nodeset_string (n), "0-2,4", "orig unaffected by changes in dup");
    like (nodeset_string (n2), "0-2,5", "dup unaffected by changes in orig");
    nodeset_destroy (n);
    nodeset_destroy (n2);

/********************************************/

    /* Try zero padding.
     */
    n = nodeset_create_string ("[1,3,5,6-100]");
    ok (n != NULL);
    nodeset_config_brackets (n, false);
    like (nodeset_string (n), "1,3,5-100", "results not zero padded by default");
    //nodeset_config_padding (n, log10 (nodeset_max (n)) + 1);
    nodeset_config_padding (n, 3);
    like (nodeset_string (n), "001,003,005-100", "padding 3 on all all works");
    nodeset_config_padding (n, 2);
    like (nodeset_string (n), "01,03,05-100", "padding 2 on subset works");
    nodeset_config_padding (n, 4);
    like (nodeset_string (n), "0001,0003,0005-0100", "padding 4 on all works");
    nodeset_destroy (n);

/********************************************/

    /* Add 'bigset' consecutive singletons.
     */
    n = nodeset_create ();
    ok (n != NULL);
    nodeset_config_brackets (n, false);

    ok (nodeset_resize (n, bigset), "explicitly resize to %u", bigset);

    monotime (&ts);
    for (i = 0; i < bigset; i++)
        if (!nodeset_add_rank (n, i))
            break;
    ok (i == bigset, "added %u consecutive ranks [%.2fs %u Mbytes]", bigset,
        monotime_since (ts)/1000, nodeset_getattr (n, NODESET_ATTR_BYTES)/1024);

    monotime (&ts);
    tmp = xasprintf ("0-%"PRIu32, bigset - 1);
    like (nodeset_string (n), tmp, "string conversion %s [%.2fs %u Mbytes]", tmp,
        monotime_since (ts)/1000, nodeset_getattr (n, NODESET_ATTR_BYTES)/1024);
    free (tmp);

    ok (nodeset_count (n) == bigset, "large nodeset count is sane");

    nodeset_destroy (n);

/********************************************/

    /* Add 'bigset'/2 non-consecutive singletons.
     */
    n = nodeset_create ();
    ok (n != NULL);
    nodeset_config_brackets (n, false);

    ok (nodeset_resize (n, bigset), "explicitly resize to %u", bigset);

    monotime (&ts);
    for (i = 0; i < bigset; i += 2)
        if (!nodeset_add_rank (n, i))
            break;
    ok (i == bigset,
        "added %u non-consecutive ranks [%.2fs %u Mbytes]", bigset/2,
        monotime_since (ts)/1000, nodeset_getattr (n, NODESET_ATTR_BYTES)/1024);

    monotime (&ts);
    ok (nodeset_string (n) != NULL, "string conversion [%.2fs %u Mbytes]",
        monotime_since (ts)/1000, nodeset_getattr (n, NODESET_ATTR_BYTES)/1024);

    ok (nodeset_count (n)  == bigset/2, "large nodeset count is sane");

    nodeset_destroy (n);

/********************************************/

    /* Check edge cases with very big ranks and resize.
     */
    bool skip_huge = true;


    n = nodeset_create ();
    nodeset_config_brackets (n, false);
    ok (nodeset_getattr (n, NODESET_ATTR_SIZE) == minsize,
        "veb size is the minimum %u", minsize);

    monotime (&ts);
    ok (!nodeset_add_rank (n, maxrank + 1),
        "adding max+1 %u rank fails [%.2fs %u Mbytes]", maxrank + 1,
        monotime_since (ts)/1000,
        nodeset_getattr (n, NODESET_ATTR_BYTES)/(1024*1024));
    ok (nodeset_getattr (n, NODESET_ATTR_SIZE) == minsize,
        "veb size is the minimum %u", minsize);

    skip (skip_huge, 16, "too slow");

    monotime (&ts);
    ok (nodeset_add_rank (n, maxrank),
        "add max rank %u [%.2fs %u Mbytes]", maxrank,
        monotime_since (ts)/1000,
        nodeset_getattr (n, NODESET_ATTR_BYTES)/(1024*1024));
    ok (nodeset_getattr (n, NODESET_ATTR_SIZE) == maxsize,
        "veb size is the maximum %u", maxsize);
    /* 2 */

    monotime (&ts);
    ok (nodeset_add_rank (n, maxrank - 1),
        "add max-1 %u [%.2fs %u Mbytes]", maxrank - 1,
        monotime_since (ts)/1000,
        nodeset_getattr (n, NODESET_ATTR_BYTES)/(1024*1024));

    ok (nodeset_test_rank (n, maxrank - 1), "test rank max - 1");
    ok (nodeset_test_rank (n, maxrank), "test rank max");
    ok (!nodeset_test_rank (n, maxrank + 1), "test rank max + 1");
    ok (nodeset_count (n) == 2, "nodeset count is sane");
    /* 7 */

    tmp = xasprintf ("%"PRIu32"-%"PRIu32, maxrank-1, maxrank);
    monotime (&ts);
    like (nodeset_string (n), tmp, "convert to string %s [%.2fs %u Mbytes]", tmp,
        monotime_since (ts)/1000,
        nodeset_getattr (n, NODESET_ATTR_BYTES)/(1024*1024));
    free (tmp);
    /* 8 */

    ok (nodeset_resize (n, 0), "resize to 0 returns success");
    ok (nodeset_getattr (n, NODESET_ATTR_SIZE) == maxsize,
        "nodeset size remains max %u", maxsize);
    /* 10 */

    nodeset_delete_rank (n, maxrank - 1);
    ok (!nodeset_test_rank (n, maxrank - 1), "nodeset_del max - 1 works");
    ok (nodeset_test_rank (n, maxrank));
    ok (!nodeset_test_rank (n, maxrank + 1));
    /* 13 */

    nodeset_delete_rank (n, maxrank + 1);
    ok (!nodeset_test_rank (n, maxrank - 1), "nodeset_del max + 1 has no effect");
    ok (nodeset_test_rank (n, maxrank));
    ok (!nodeset_test_rank (n, maxrank + 1));
    /* 16 */

    end_skip;

    nodeset_delete_rank (n, maxrank);
    ok (!nodeset_test_rank (n, maxrank - 1), "nodeset_del max works");
    ok (!nodeset_test_rank (n, maxrank));
    ok (!nodeset_test_rank (n, maxrank + 1));
    /* 19 */

    ok (nodeset_resize (n, 0), "resize to zero returns success");
    ok (nodeset_getattr (n, NODESET_ATTR_SIZE) == minsize,
        "nodeset size is the minimum %u", minsize);

    nodeset_destroy (n);

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
