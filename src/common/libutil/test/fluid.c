#include "src/common/libtap/tap.h"
#include "src/common/libutil/fluid.h"

int main (int argc, char *argv[])
{
    struct fluid_generator gen;
    fluid_t id, id2;
    char buf[1024];
    int i;
    int generate_errors;
    int encode_errors;
    int decode_errors;

    plan (NO_PLAN);

    ok (fluid_init (&gen, 0) == 0,
        "fluid_init id=0 works");

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
    gen.id = 16383;
    gen.epoch -= 1000ULL*60*60*24*365*34; // 34 years
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
     * Probably will induce usleep and take around 64 milliseconds.
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

    /* Continue for another 4K with NMEMONIC encoding, which is slower
     * and probably won't induce usleep.
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

    /* Decode bad input must fail
     */
    ok (fluid_decode ("bogus", &id, FLUID_STRING_DOTHEX) < 0,
        "fluid_decode type=DOTHEX fails on input=bogus");
    ok (fluid_decode ("bogus", &id, FLUID_STRING_MNEMONIC) < 0,
        "fluid_decode type=MNEMONIC fails on input=bogus");
    ok (fluid_decode ("a-a-a--a-a-a", &id, FLUID_STRING_MNEMONIC) < 0,
        "fluid_decode type=MNEMONIC fails on unknown words xx-xx-xx--xx-xx-xx");

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
