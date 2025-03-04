/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <string.h>
#include <locale.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/fluid.h"
#include "ccan/str/str.h"

struct f58_test {
    fluid_t id;
    const char *f58;
    const char *f58_alt;
};

struct f58_test f58_tests [] = {
#if !ASSUME_BROKEN_LOCALE
    { 0, "ƒ1", "f1" },
    { 1, "ƒ2", "f2" },
    { 57, "ƒz", "fz" },
    { 1234, "ƒNH", "fNH" },
    { 1888, "ƒZZ", "fZZ" },
    { 3363, "ƒzz", "fzz" },
    { 3364, "ƒ211", "f211" },
    { 4369, "ƒ2JL", "f2JL" },
    { 65535, "ƒLUv", "fLUv" },
    { 4294967295, "ƒ7YXq9G", "f7YXq9G" },
    { 633528662, "ƒxyzzy", "fxyzzy" },
    { 6731191091817518LL, "ƒuZZybuNNy", "fuZZybuNNy" },
    { 18446744073709551614UL, "ƒjpXCZedGfVP", "fjpXCZedGfVP" },
    { 18446744073709551615UL, "ƒjpXCZedGfVQ", "fjpXCZedGfVQ" },
#endif
    { 0, NULL, NULL },
};

struct f58_test f58_alt_tests [] = {
    { 0, "f1", NULL },
    { 0, "f111", NULL },
    { 1, "f2", NULL },
    { 57, "fz", NULL },
    { 1234, "fNH", NULL },
    { 1888, "fZZ", NULL },
    { 3363, "fzz", NULL },
    { 3364, "f211", NULL },
    { 4369, "f2JL", NULL },
    { 65535, "fLUv", NULL },
    { 4294967295, "f7YXq9G", NULL },
    { 633528662, "fxyzzy", NULL },
    { 6731191091817518LL, "fuZZybuNNy", NULL },
    { 18446744073709551614UL, "fjpXCZedGfVP", NULL },
    { 18446744073709551615UL, "fjpXCZedGfVQ", NULL },
    { 0, NULL, NULL },
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
        if (streq (buf, tp->f58)
            || streq (buf, tp->f58_alt))
            pass ("f58_encode %ju -> %s", tp->id, buf);
        else
            fail ("f58_encode %ju: got %s expected %s",
                  tp->id, buf, tp->f58);
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

#if !ASSUME_BROKEN_LOCALE
    if (setenv ("FLUX_F58_FORCE_ASCII", "1", 1) < 0)
        BAIL_OUT ("Failed to setenv FLUX_F58_FORCE_ASCII");
    ok (fluid_encode (buf, sizeof (buf), f58_tests->id, type) == 0,
        "fluid_encode with FLUX_F58_FORCE_ASCII works");
    is (buf, f58_tests->f58_alt,
        "fluid_encode with FLUX_F58_FORCE_ASCII used ascii prefix");
    if (unsetenv ("FLUX_F58_FORCE_ASCII") < 0)
        BAIL_OUT ("Failed to unsetenv FLUX_F58_FORCE_ASCII");

    ok (fluid_encode (buf,
                      sizeof (buf),
                      f58_tests->id,
                      FLUID_STRING_F58_PLAIN) == 0,
        "fluid_encode FLUX_STRING_F58_PLAIN works");
    is (buf, f58_tests->f58_alt,
        "fluid_encode FLUID_STRING_F58_PLAIN used ascii prefix");
#endif

    ok (fluid_encode (buf, 1, 1, type) < 0 && errno == EOVERFLOW,
        "fluid_encode (buf, 1, 1, F58) returns EOVERFLOW");
    errno = 0;
    ok (fluid_encode (buf, 4, 65535, type) < 0 && errno == EOVERFLOW,
        "fluid_encode (buf, 4, 65535, F58) returns EOVERFLOW: %s", strerror (errno));

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

struct fluid_parse_test {
    fluid_t id;
    const char *input;
};

struct fluid_parse_test fluid_parse_tests [] = {
#if !ASSUME_BROEN_LOCALE
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
#endif
    { 0, "f1" },
    { 1, "f2" },
    { 4294967295, "f7YXq9G" },
    { 633528662, "fxyzzy" },
    { 18446744073709551614UL, "fjpXCZedGfVP" },
    { 18446744073709551615UL, "fjpXCZedGfVQ" },
    { 1234, "1234" },
    { 1888, "1888" },
    { 3363, "3363" },
    { 3364, "3364" },
    { 4369, "4369" },
    { 6731191091817518LL, "6731191091817518" },
    { 18446744073709551614UL, "18446744073709551614" },
    { 18446744073709551615UL, "18446744073709551615" },
    { 0, "0x0" },
    { 1, "0x1" },
    { 57, "0x39" },
    { 1234, "0x4d2" },
    { 1888, "0x760" },
    { 3363, "0xd23" },
    { 4369, "0x1111" },
    { 65535, "0xffff" },
    { 4294967295, "0xffffffff" },
    { 633528662,  "0x25c2e156" },
    { 6731191091817518LL, "0x17e9fb8df16c2e" },
    { 18446744073709551615UL, "0xffffffffffffffff" },
    { 0, "0.0.0.0" },
    { 1, "0000.0000.0000.0001" },
    { 57, "0.0.0.0039" },
    { 1234, "0000.0000.0000.04d2" },
    { 1888, "0000.0000.0000.0760" },
    { 4369, "0000.0000.0000.1111" },
    { 65535, "0.0.0.ffff" },
    { 4294967295, "0000.0000.ffff.ffff" },
    { 18446744073709551615UL, "ffff.ffff.ffff.ffff" },
    { 0, "😃" },
    { 1, "😄" },
    { 57, "🙊" },
    { 1234, "😁👌" },
    { 1888, "😆🐻" },
    { 4369, "😊🌀" },
    { 65535, "💁📚" },
    { 4294967295, "😳🏪🍖🍸" },
    { 18446744073709551615UL, "🚹💗💧👗😷📷📚" },
    { 0, NULL },
};

static void test_fluid_parse (void)
{
    fluid_t id;
    struct fluid_parse_test *tp = fluid_parse_tests;
    while (tp->input != NULL) {
        id = 0;
        ok (fluid_parse (tp->input, &id) == 0,
            "fluid_parse (%s) works", tp->input);
        ok (id == tp->id,
            "%s -> %ju", tp->input, (uintmax_t) id);
        tp++;
    }

    ok (fluid_parse (" 0xffff   ", &id) == 0,
        "flux_parse() works with leading/trailing whitespace");
    ok (id == 65535,
        "flux_parse with whitespace works");

    id = 0;
    ok (fluid_parse (NULL, &id) < 0 && errno == EINVAL,
        "fluid_parse returns EINVAL for with NULL string");
    ok (fluid_parse ("", &id) < 0 && errno == EINVAL,
        "fluid_parse returns EINVAL for with empty string");
    ok (fluid_parse ("boo", &id) < 0 && errno == EINVAL,
        "fluid_parse returns EINVAL for 'boo'");
    ok (fluid_parse ("f", &id) < 0 && errno == EINVAL,
        "fluid_parse returns EINVAL for 'f'");
    ok (fluid_parse ("-1", &id) < 0 && errno == EINVAL,
        "fluid_parse returns EINVAL for '-1'");
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
        "fluid_decode type=DOTHEX works");
    diag ("%s", buf);

    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_MNEMONIC) == 0,
        "fluid_encode type=MNEMONIC works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_MNEMONIC) == 0 && id == id2,
        "fluid_decode type=MNEMONIC works");
    diag ("%s", buf);

    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_EMOJI) == 0,
        "fluid_encode type=EMOJI works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_EMOJI) == 0 && id == id2,
        "fluid_decode type=EMOJI works");
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
        "fluid_decode type=DOTHEX works");
    diag ("%s", buf);

    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_MNEMONIC) == 0,
        "fluid_encode type=MNEMONIC works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_MNEMONIC) == 0 && id == id2,
        "fluid_decode type=MNEMONIC works");
    diag ("%s", buf);

    ok (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_EMOJI) == 0,
        "fluid_encode type=EMOJI works");
    ok (fluid_decode (buf, &id2, FLUID_STRING_EMOJI) == 0 && id == id2,
        "fluid_decode type=EMOJI works");
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

    /* Continue for another 4K with MNEMONIC encoding (slower).
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

    /* Continue for another 4K with EMOJI encoding (slower).
     */
    generate_errors = 0;
    encode_errors = 0;
    decode_errors = 0;
    for (i = 0; i < 4096; i++) {
        if (fluid_generate (&gen, &id) < 0)
            generate_errors++;
        if (fluid_encode (buf, sizeof (buf), id, FLUID_STRING_EMOJI) < 0)
            encode_errors++;
        if (fluid_decode (buf, &id2, FLUID_STRING_EMOJI) < 0 || id != id2)
            decode_errors++;
    }
    ok (generate_errors == 0,
        "fluid_generate worked 4K times");
    ok (encode_errors == 0,
        "fluid_encode type=EMOJI worked 4K times");
    ok (decode_errors == 0,
        "fluid_decode type=EMOJI worked 4K times");


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
    ok (fluid_decode ("bogus", &id, FLUID_STRING_EMOJI) < 0,
        "fluid_decode type=EMOJI fails on ascii string");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    /* Locale initialization required for fluid_f58_encode() */
    setlocale (LC_ALL, "");

    test_basic ();
    test_f58 ();
    test_fluid_parse ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
