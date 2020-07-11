/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/fluid.h"

struct f58_test {
    fluid_t id;
    const char *f58;
};

struct f58_test f58_tests [] = {
    { 0, "ƒ1" },
    { 1, "ƒ2" },
    { 57, "ƒz" },
    { 1234, "ƒNH" },
    { 1888, "ƒZZ" },
    { 3363, "ƒzz" },
    { 3364, "ƒ211" },
    { 4369, "ƒ2JL" },
    { 65535, "ƒLUv" },
    { 4294967295, "ƒ7YXq9G" },
    { 633528662, "ƒxyzzy" },
    { 6731191091817518LL, "ƒuZZybuNNy" },
    { 18446744073709551614UL, "ƒjpXCZedGfVP" },
    { 18446744073709551615UL, "ƒjpXCZedGfVQ" },
    { 0, NULL },
};

struct f58_test f58_alt_tests [] = {
    { 0, "f1" },
    { 0, "f111" },
    { 1, "f2" },
    { 57, "fz" },
    { 1234, "fNH" },
    { 1888, "fZZ" },
    { 3363, "fzz" },
    { 3364, "f211" },
    { 4369, "f2JL" },
    { 65535, "fLUv" },
    { 4294967295, "f7YXq9G" },
    { 633528662, "fxyzzy" },
    { 6731191091817518LL, "fuZZybuNNy" },
    { 18446744073709551614UL, "fjpXCZedGfVP" },
    { 18446744073709551615UL, "fjpXCZedGfVQ" },
    { 0, NULL },
};

void test_f58 (void)
{
    fluid_string_type_t type = FLUID_STRING_F58;
    char buf[16];
    fluid_t id;
    struct f58_test *tp = f58_tests;
    while (tp->f58 != NULL) {
        ok (fluid_encode (buf, sizeof(buf), tp->id, type) == 0,
            "f58_encode (%ju)", tp->id);
        is (buf, tp->f58,
            "f58_encode %ju -> %s", tp->id, buf);
        ok (fluid_decode (tp->f58, &id, type) == 0,
            "f58_decode (%s)", tp->f58);
        ok (id == tp->id,
            "%s -> %ju", tp->f58, (uintmax_t) id);
        tp++;
    }
    tp = f58_alt_tests;
    while (tp->f58 != NULL) {
        ok (fluid_decode (tp->f58, &id, type) == 0,
            "f58_decode (%s)", tp->f58);
        ok (id == tp->id,
            "%s -> %ju", tp->f58, (uintmax_t) id);
        tp++;
    }

    ok (fluid_encode (buf, 1, 1, type) < 0 && errno == EOVERFLOW,
        "fluid_encode (buf, 1, 1, F58) returns EOVERFLOW");
    ok (fluid_encode (buf, 5, 65535, type) < 0 && errno == EOVERFLOW,
        "fluid_encode (buf, 5, 65535, F58) returns EOVERFLOW");

    ok (fluid_decode ("1234", &id, type) < 0 && errno == EINVAL,
        "fluid_decode ('aaa', FLUID_STRING_F58) returns EINVAL");
    ok (fluid_decode ("aaa", &id, type) < 0 && errno == EINVAL,
        "fluid_decode ('aaa', FLUID_STRING_F58) returns EINVAL");
    ok (fluid_decode ("f", &id, type) < 0 && errno == EINVAL,
        "fluid_decode ('f', FLUID_STRING_F58) returns EINVAL");
    ok (fluid_decode ("flux", &id, type) < 0 && errno == EINVAL,
        "fluid_decode ('flux', FLUID_STRING_F58) returns EINVAL");
    ok (fluid_decode ("f1230", &id, type) < 0 && errno == EINVAL,
        "fluid_decode ('f1230', FLUID_STRING_F58) returns EINVAL");
    ok (fluid_decode ("x1", &id, type) < 0 && errno == EINVAL,
        "fluid_decode ('x1', FLUID_STRING_F58) returns EINVAL");
}

void test_basic (void)
{
    struct fluid_generator gen;
    fluid_t id, id2;
    char buf[1024];
    int i;
    int generate_errors;
    int encode_errors;
    int decode_errors;

    ok (fluid_init (&gen, 0, 0) == 0,
        "fluid_init id=0 timestamp=0 works");

    /* Probably all zeroes, or (unlikely) with slightly advanced timestamp.
     */
    ok (fluid_generate (&gen, &id) == 0,
        "fluid_generate works first time");
    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_DOTHEX) == 0,
        "fluid_encode type=DOTHEX works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_DOTHEX) == 0 && id == id2,
        "fluid_decode type=MNEMONIC works");
    diag ("%s", buf);

    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_MNEMONIC) == 0,
        "fluid_encode type=MNEMONIC works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_MNEMONIC) == 0 && id == id2,
        "fluid_decode type=MNEMONIC works");
    diag ("%s", buf);

    /* With artificially tweaked generator state
     */
    const uint64_t time_34y = 1000ULL*60*60*24*365*34;
    ok (fluid_init (&gen, 0, time_34y) == 0,
        "fluid_init id=0 timestamp=34y works");
    gen.id = 16383;
    gen.seq = 1023;
    ok (fluid_generate (&gen, &id) == 0,
        "fluid_generate works 34 years in the future");
    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_DOTHEX) == 0,
        "fluid_encode type=DOTHEX works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_DOTHEX) == 0 && id == id2,
        "fluid_decode type=MNEMONIC works");
    diag ("%s", buf);

    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_MNEMONIC) == 0,
        "fluid_encode type=MNEMONIC works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_MNEMONIC) == 0 && id == id2,
        "fluid_decode type=MNEMONIC works");
    diag ("%s", buf);

    /* Generate 64K id's as rapidly as possible.
     * Probably will cover running out of seq bits.
     */
    generate_errors = 0;
    encode_errors = 0;
    decode_errors = 0;
    for (i = 0; i < 65536; i++) {
        if (fluid_generate (&gen, &id) < 0)
            generate_errors++;
        if (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_DOTHEX) < 0)
            encode_errors++;
        if (fluid_decode (buf, &id2, FLUID_STRING_DOTHEX) < 0 || id != id2)
            decode_errors++;
    }
    ok (generate_errors == 0,
        "fluid_generate worked 64K times in tight loop");
    ok (encode_errors == 0,
        "fluid_encode type=DOTHEX worked 64K times");
    ok (decode_errors == 0,
        "fluid_decode type=DOTHEX worked 64K times");

    /* Continue for another 4K with NMEMONIC encoding (slower).
     */
    generate_errors = 0;
    encode_errors = 0;
    decode_errors = 0;
    for (i = 0; i < 4096; i++) {
        if (fluid_generate (&gen, &id) < 0)
            generate_errors++;
        if (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_MNEMONIC) < 0)
            encode_errors++;
        if (fluid_decode (buf, &id2, FLUID_STRING_MNEMONIC) < 0 || id != id2)
            decode_errors++;
    }
    ok (generate_errors == 0,
        "fluid_generate worked 4K times");
    ok (encode_errors == 0,
        "fluid_encode type=MNEMONIC worked 4K times");
    ok (decode_errors == 0,
        "fluid_decode type=MNEMONIC worked 4K times");

    /* Generate 64K FLUIDs, restarting generator each time from timestamp
     * extracted from generated FLUID + 1.  Verify number always increases.
     */
    fluid_t lastid = 0;
    uint64_t ts;
    int errors = 0;
    for (i = 0; i < 65536; i++) {
        if (fluid_generate (&gen, &id) < 0)
            BAIL_OUT ("fluid_generate unexpectedly failed");
        if (lastid >= id)
            errors++;
        lastid = id;
        ts = fluid_get_timestamp (id);
        if (fluid_init (&gen, 0, ts + 1) < 0)
            BAIL_OUT ("fluid_init unexpectedly failed");
    }
    ok (errors == 0,
        "restarted generator 64K times without going backwards");

    /* Get timestsamp with fluid_save()
     */
    ok (fluid_save_timestamp (&gen, &ts) == 0 && ts == gen.timestamp,
        "fluid_save_timestamp worked");

    /* Decode bad input must fail
     */
    ok (fluid_decode ("bogus", &id, FLUID_STRING_DOTHEX) < 0,
        "fluid_decode type=DOTHEX fails on input=bogus");
    ok (fluid_decode ("bogus", &id, FLUID_STRING_MNEMONIC) < 0,
        "fluid_decode type=MNEMONIC fails on input=bogus");
    ok (fluid_decode ("a-a-a--a-a-a", &id, FLUID_STRING_MNEMONIC) < 0,
        "fluid_decode type=MNEMONIC fails on unknown words xx-xx-xx--xx-xx-xx");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_f58 ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
