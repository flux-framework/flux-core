#include "catch.hpp"

extern "C" {
#include "nodeset.h"
#include "xzmalloc.h"
#include "monotime.h"
}

// Expected failure skipped using "hide" tag
TEST_CASE ("expected failing test", "[hide]") {
    REQUIRE (false == true);
}

TEST_CASE ("basic nodeset operations are sane", "[nodeset]") {

    std::string s;
    nodeset_t n = nodeset_new ();
    REQUIRE (n != NULL);
    nodeset_conf_brackets (n, false);

    // Add three consecutive ranks.
    // 1 list entry, 3 ranks
    nodeset_add_rank (n, 8);
    nodeset_add_rank (n, 7);
    nodeset_add_rank (n, 9);
    s = nodeset_str (n);
    REQUIRE (s == "7-9");
    REQUIRE (nodeset_count (n) == 3);

    // Add one disjoint rank before.
    // 2 list entries, 4 ranks
    nodeset_add_rank (n, 1);
    s = nodeset_str (n);
    REQUIRE (s == "1,7-9");
    REQUIRE (nodeset_count (n) == 4);

    // Add one disjoint rank after.
    // 3 list entries, 5 ranks
    nodeset_add_rank (n, 16);
    s = nodeset_str (n);
    REQUIRE (s == "1,7-9,16");
    REQUIRE (nodeset_count (n) == 5);

    // Insert one disjoint rank in the middle.
    // 4 list entries, 6 ranks
    nodeset_add_rank (n, 14);
    s = nodeset_str (n);
    REQUIRE (s == "1,7-9,14,16");
    REQUIRE (nodeset_count (n) == 6);

    // Insert one disjoint rank in the middle.
    // 5 list entries, 7 ranks
    nodeset_add_rank (n, 3);
    s = nodeset_str (n);
    REQUIRE (s == "1,3,7-9,14,16");
    REQUIRE (nodeset_count (n) == 7);

    // Insert range that contains two singletons.
    // 4 list entries, 8 ranks
    nodeset_add_range (n, 1, 3);
    s = nodeset_str (n);
    REQUIRE (s == "1-3,7-9,14,16");
    REQUIRE (nodeset_count (n) == 8);

    // Insert range overlapping range on low side
    // 4 list entries, 10 ranks
    nodeset_add_range (n, 5, 8);
    s = nodeset_str (n);
    REQUIRE (s == "1-3,5-9,14,16");
    REQUIRE (nodeset_count (n) == 10);

    // Insert range overlapping range on high side
    // 4 list entries, 12 ranks
    nodeset_add_range (n, 8, 11);
    s = nodeset_str (n);
    REQUIRE (s == "1-3,5-11,14,16");
    REQUIRE (nodeset_count (n) == 12);

    // Add range that contains several singletons and ranges.
    // 1 list entries, 16 ranks
    nodeset_add_range (n, 1, 16);
    s = nodeset_str (n);
    REQUIRE (s == "1-16");
    REQUIRE (nodeset_count (n) == 16);

    // Add a range contained in existing range.
    // No change.
    nodeset_add_range (n, 4, 8);
    s = nodeset_str (n);
    REQUIRE (s == "1-16");
    REQUIRE (nodeset_count (n) == 16);

    nodeset_destroy (n);
}

TEST_CASE ("edge: add 0,1,2, see if 1 merges with 0", "[nodeset]") {

    std::string s;
    nodeset_t n = nodeset_new ();
    REQUIRE (n != NULL);

    nodeset_add_rank (n, 0);
    nodeset_add_rank (n, 1);
    nodeset_add_rank (n, 2);
    s = nodeset_str (n);
    REQUIRE (s == "[0-2]");
    REQUIRE (nodeset_count (n) == 3);

    nodeset_conf_ranges (n, false);
    s = nodeset_str (n);
    REQUIRE (s == "[0,1,2]");

    nodeset_destroy (n);
}

TEST_CASE ("edge: add 2,1,0, see if 1 merges with 0", "[nodeset]") {

    std::string s;
    nodeset_t n = nodeset_new ();
    REQUIRE (n != NULL);

    nodeset_add_rank (n, 2);
    nodeset_add_rank (n, 1);
    nodeset_add_rank (n, 0);
    s = nodeset_str (n);
    REQUIRE (s == "[0-2]");
    REQUIRE (nodeset_count (n) == 3);

    nodeset_destroy (n);
}

TEST_CASE ("nodeset_new_str", "[nodeset]") {

    std::string s;
    nodeset_t n;

    n = nodeset_new_str ("[1,3,5,6-100]");
    REQUIRE (n != NULL);
    s = nodeset_str (n);
    REQUIRE (s == "[1,3,5-100]");
    REQUIRE (nodeset_count (n) == 98);
    nodeset_destroy (n);

    n = nodeset_new_str ("2-1");
    REQUIRE (n != NULL);
    s = nodeset_str (n);
    REQUIRE (s == "[1-2]");
    REQUIRE (nodeset_count (n) == 2);
    nodeset_destroy (n);

    n = nodeset_new_str ("");
    REQUIRE (n != NULL);
    REQUIRE (nodeset_count (n) == 0);
    s = nodeset_str (n);
    REQUIRE (s == "");
    nodeset_destroy (n);

    n = nodeset_new_str ("[1-2]");
    REQUIRE (n != NULL);
    REQUIRE (nodeset_count (n) == 2);
    nodeset_destroy (n);

    n = nodeset_new_str (",");
    REQUIRE (n == NULL);

    n = nodeset_new_str ("-1");
    REQUIRE (n == NULL);

    n = nodeset_new_str ("1-");
    REQUIRE (n == NULL);

    n = nodeset_new_str ("foo1");
    REQUIRE (n == NULL);

    n = nodeset_new_str ("xyz");
    REQUIRE (n == NULL);
}

TEST_CASE ("nodeset_del, nodeset_test_rank, nodeset_test_range", "[nodeset]") {

    std::string s;
    nodeset_t n = nodeset_new_str ("0-2");

    REQUIRE (n != NULL);
    s = nodeset_str (n);
    REQUIRE (s == "[0-2]");
    REQUIRE (nodeset_test_range (n, 0, 2));
    nodeset_del_rank (n, 0);
    REQUIRE (!nodeset_test_rank (n, 0));
    REQUIRE (nodeset_test_range (n, 1, 2));
    s = nodeset_str (n);
    REQUIRE (s == "[1-2]");
    nodeset_del_rank (n, 1);
    REQUIRE (!nodeset_test_rank (n, 0));
    REQUIRE (!nodeset_test_rank (n, 1));
    REQUIRE (nodeset_test_rank (n, 2));
    s = nodeset_str (n);
    REQUIRE (s == "2");
    nodeset_del_rank (n, 2);
    //assert (!n->s_valid);
    REQUIRE (!nodeset_test_rank (n, 0));
    REQUIRE (!nodeset_test_rank (n, 1));
    REQUIRE (!nodeset_test_rank (n, 2));
    s = nodeset_str (n);
    REQUIRE (s == "");

    nodeset_destroy (n);
}

TEST_CASE ("nodeset_next, nodeset_itr_rewind", "[nodeset]") {

    std::string s;
    nodeset_t n = nodeset_new_str ("0-2");
    REQUIRE (n != NULL);
    nodeset_itr_t itr = nodeset_itr_new (n);
    REQUIRE (itr != NULL);

    REQUIRE (nodeset_next (itr) == 0);
    REQUIRE (nodeset_next (itr) == 1);
    REQUIRE (nodeset_next (itr) == 2);
    REQUIRE (nodeset_next (itr) == NODESET_EOF);
    nodeset_itr_rewind (itr);
    REQUIRE (nodeset_next (itr) == 0);

    nodeset_itr_destroy (itr);
    nodeset_destroy (n);
}

TEST_CASE ("nodeset_dup", "[nodeset]") {

    std::string s;
    nodeset_t n = nodeset_new_str ("0-2");
    REQUIRE (n != NULL);

    s = nodeset_str (n);
    REQUIRE (s == "[0-2]");

    nodeset_t n2 = nodeset_dup (n);
    REQUIRE (n2 != NULL);

    s = nodeset_str (n2);
    REQUIRE (s == "[0-2]");

    nodeset_add_rank (n, 4);
    nodeset_add_rank (n2, 5);
    s = nodeset_str (n);
    REQUIRE (s == "[0-2,4]");
    s = nodeset_str (n2);
    REQUIRE (s == "[0-2,5]");

    nodeset_destroy (n);
    nodeset_destroy (n2);
}

TEST_CASE ("nodeset_conf_padding", "[nodeset]") {

    std::string s;
    nodeset_t n = nodeset_new_str ("[1,3,5,6-100]");

    s = nodeset_str (n);
    REQUIRE (s == "[1,3,5-100]");
    //nodeset_conf_padding (n, log10 (nodeset_max (n)) + 1);

    nodeset_conf_padding (n, 3);
    s = nodeset_str (n);
    REQUIRE (s == "[001,003,005-100]");

    nodeset_conf_padding (n, 2);
    s = nodeset_str (n);
    REQUIRE (s == "[01,03,05-100]");

    nodeset_conf_padding (n, 4);
    s = nodeset_str (n);
    REQUIRE (s == "[0001,0003,0005-0100]");

    nodeset_destroy (n);
}

TEST_CASE ("1E6 consecutive singletons", "[nodeset]") {

    const uint32_t bigset = 1E6;
    struct timespec ts;

    monotime (&ts);
    nodeset_t n = nodeset_new ();
    std::string s, tmp;
    uint32_t i;
    double t;

    nodeset_resize (n, bigset);
    for (i = 0; i < bigset; i++)
        nodeset_add_rank (n, i);
    t = monotime_since (ts)/1000;
    INFO ("add " << bigset << " consecutive: " << t << "s");

    monotime (&ts);
    (void)nodeset_str (n); /* time the work */
    t = monotime_since (ts)/1000;
    INFO ("tostr " << bigset << " consecutive: " << t << "s");

    tmp = xasprintf ("[0-%u]", bigset - 1);
    s = nodeset_str (n);
    REQUIRE (s == tmp);
    REQUIRE (nodeset_count (n) == bigset);

    nodeset_destroy (n);
}

TEST_CASE ("5E5 non-consecutive singletons", "[nodeset]") {

    const uint32_t bigset = 1E6;
    struct timespec ts;

    monotime (&ts);
    nodeset_t n = nodeset_new ();
    std::string s, tmp;
    uint32_t i;
    double t;

    nodeset_resize (n, bigset);
    for (i = 0; i < bigset; i += 2)
        nodeset_add_rank (n, i);
    t = monotime_since (ts)/1000;
    INFO ("add " << bigset << " non-consecutive: " << t << "s");

    monotime (&ts);
    (void)nodeset_str (n); /* time the work */
    t = monotime_since (ts)/1000;
    INFO ("tostr " << bigset << " non-consecutive: " << t << "s");

    REQUIRE (nodeset_count (n)  == bigset/2);

    nodeset_destroy (n);
}

// This test case may fail on low-memory systems,
// for example a build farm VM, so don't run them by default
TEST_CASE ("edge: resize largest ranks", "[hide]") {

    const uint32_t ABS_MAX_SIZE = (~(uint32_t)0);
    const uint32_t ABS_MAX_RANK = (~(uint32_t)0 - 1);
    const uint32_t r = ABS_MAX_RANK;
    struct timespec ts;
    double t;
    std::string s, tmp;

    monotime (&ts);
    nodeset_t n = nodeset_new ();

    REQUIRE (nodeset_add_rank (n, r + 1) == false);
    REQUIRE (nodeset_add_rank (n, r) == true);
    //REQUIRE (NS_SIZE (n) == ABS_MAX_SIZE); // should have triggered expansion
    REQUIRE (nodeset_add_rank (n, r - 1) == true);
    t = monotime_since (ts)/1000;
    INFO ("set rank " << r-1 << "," << r << "," << r+1 << " " << t << "s")

    REQUIRE (nodeset_test_rank (n, r - 1));
    REQUIRE (nodeset_test_rank (n, r));
    REQUIRE (!nodeset_test_rank (n, r + 1));
    REQUIRE (nodeset_count (n) == 2);

    monotime (&ts);
    (void)nodeset_str (n);
    t = monotime_since (ts) / 1000;
    INFO ("tostr " << t << "s");

    tmp = xasprintf ("[%u-%u]", r-1, r);
    s = nodeset_str (n);
    REQUIRE (tmp == s);

    REQUIRE (nodeset_resize (n, 0) == true); // should have no effect
    //REQUIRE (NS_SIZE (n) == ABS_MAX_SIZE);

    nodeset_del_rank (n, r - 1);
    REQUIRE (!nodeset_test_rank (n, r - 1));
    REQUIRE (nodeset_test_rank (n, r));
    REQUIRE (!nodeset_test_rank (n, r + 1));

    nodeset_del_rank (n, r + 1);
    REQUIRE (!nodeset_test_rank (n, r - 1));
    REQUIRE (nodeset_test_rank (n, r));
    REQUIRE (!nodeset_test_rank (n, r + 1));

    nodeset_del_rank (n, r);
    REQUIRE (!nodeset_test_rank (n, r - 1));
    REQUIRE (!nodeset_test_rank (n, r));
    REQUIRE (!nodeset_test_rank (n, r + 1));

    REQUIRE (nodeset_resize (n, 0) == true); // this should shrink it
    //assert (NS_SIZE (n) == veb_minsize);

    nodeset_destroy (n);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
