/* adapted for TAP from src/common/libutil/sha1.c */
#include <string.h>
#include "src/common/libtap/tap.h"
#include "src/common/libutil/sha1.h"

/*
 * Test Vectors (from FIPS PUB 180-1)
 * "abc"
 * A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
 * "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
 * 84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
 * A million repetitions of "a"
 * 34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
 */

static char *test_data[] = {"abc",
                            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                            "A million repetitions of 'a'"};
static char *test_results[] = {"A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D",
                               "84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1",
                               "34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F"};

void digest_to_hex (const uint8_t digest[SHA1_DIGEST_SIZE], char *output)
{
    int i, j;
    char *c = output;

    for (i = 0; i < SHA1_DIGEST_SIZE / 4; i++) {
        for (j = 0; j < 4; j++) {
            sprintf (c, "%02X", digest[i * 4 + j]);
            c += 2;
        }
        sprintf (c, " ");
        c += 1;
    }
    *(c - 1) = '\0';
}

int main (int argc, char **argv)
{
    int k;
    SHA1_CTX context;
    uint8_t digest[20];
    char output[80];

    plan (NO_PLAN);

    for (k = 0; k < 2; k++) {
        SHA1_Init (&context);
        SHA1_Update (&context, (uint8_t *)test_data[k], strlen (test_data[k]));
        SHA1_Final (&context, digest);
        digest_to_hex (digest, output);

        ok (strcmp (output, test_results[k]) == 0, "FIPS test vector %s", test_data[k]);
    }

    /* million 'a' vector we feed separately */
    SHA1_Init (&context);
    for (k = 0; k < 1000000; k++)
        SHA1_Update (&context, (uint8_t *)"a", 1);
    SHA1_Final (&context, digest);
    digest_to_hex (digest, output);
    ok (strcmp (output, test_results[2]) == 0, "FIPS test vector %s", test_data[2]);

    /* verify that (>200 byte) data buffer isn't scribbled upon
     * N.B. if sha1.c is built without SHA1HANDSOFF, this fails.
     */
    const int blobsize = 1024;
    uint8_t refblob[blobsize];
    uint8_t blob[blobsize];
    memset (refblob, 1, sizeof (blob));
    memcpy (blob, refblob, sizeof (blob));
    SHA1_Init (&context);
    SHA1_Update (&context, blob, blobsize);
    SHA1_Final (&context, digest);
    ok (memcmp (blob, refblob, blobsize) == 0,
        "%d byte buffer was not scribbled upon during SHA1 computation",
        blobsize);

    done_testing ();
}
