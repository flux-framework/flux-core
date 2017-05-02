#include <string.h>
#include <errno.h>
#include "src/common/libtap/tap.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/sha1.h"
#include "src/common/libutil/sha256.h"

const char *badref[] = {
    "nerf-4d4ed591f7d26abd8145650f334d283bdb661765", // unknown hash
    "sha14d4ed591f7d26abd8145650f334d283bdb661765",  // missing prefix sep
    "sha256-4d4ed591f7d26abd8145650f334d283bdb661765", // runt
    "sha1-4d4ed591f7d26abd8145650f334d283bdb66176x", // suffix chars not hex */
    NULL,
};

const char *goodref[] = {
    "sha1-4d4ed591f7d26abd8145650f334d283bdb661765",
    "sha256-a99c07ce93703c7390589c5b007bd9a97a8b6de29e9a920d474d4f028ce2d42c",
    NULL,
};

int main(int argc, char** argv)
{
    char ref[BLOBREF_MAX_STRING_SIZE];
    char ref2[BLOBREF_MAX_STRING_SIZE];
    uint8_t digest[BLOBREF_MAX_DIGEST_SIZE];
    uint8_t data[1024];

    plan (NO_PLAN);

    memset (data, 7, sizeof (data));

    /* invalid args */
    errno = 0;
    ok (blobref_hash ("nerf", data, sizeof (data), ref, sizeof (ref)) < 0
        && errno == EINVAL,
        "blobref_hash fails EINVAL with unknown hash name");
    errno = 0;
    ok (blobref_hash ("sha1", data, sizeof (data), ref, 2) < 0
        && errno == EINVAL,
        "blobref_hash fails EINVAL with runt ref buffer");

    errno = 0;
    ok (blobref_strtohash (badref[0], digest, sizeof (digest)) < 0
        && errno == EINVAL,
        "blobref_strtohash fails EINVAL with unknown hash prefix");
    errno = 0;
    ok (blobref_strtohash (badref[1], digest, sizeof (digest)) < 0
        && errno == EINVAL,
        "blobref_strtohash fails EINVAL with missing hash prefix separator");
    errno = 0;
    ok (blobref_strtohash (badref[2], digest, sizeof (digest)) < 0
        && errno == EINVAL,
        "blobref_strtohash fails EINVAL with wrong blobref length for prefix");
    errno = 0;
    ok (blobref_strtohash (badref[3], digest, sizeof (digest)) < 0
        && errno == EINVAL,
        "blobref_strtohash fails EINVAL with out of range blobref chars");
    errno = 0;
    ok (blobref_strtohash (goodref[0], digest, 2) < 0
        && errno == EINVAL,
        "blobref_strtohash fails EINVAL with runt digest size");

    memset (digest, 6, sizeof (digest));
    errno = 0;
    ok (blobref_hashtostr ("nerf", digest, SHA1_DIGEST_SIZE, ref, sizeof (ref)) < 0
        && errno == EINVAL,
        "blobref_hashtostr fails EINVAL with unknown hash");
    errno = 0;
    ok (blobref_hashtostr ("sha1", digest, SHA256_BLOCK_SIZE, ref, sizeof (ref)) < 0
        && errno == EINVAL,
        "blobref_hashtostr fails EINVAL with wrong digest size for hash");
    errno = 0;
    ok (blobref_hashtostr ("sha1", digest, SHA1_DIGEST_SIZE, ref, 2) < 0
        && errno == EINVAL,
        "blobref_hashtostr fails EINVAL with runt ref");

    /* sha1 */
    ok (blobref_hash ("sha1", NULL, 0, ref, sizeof (ref)) == 0,
        "blobref_hash sha1 handles zero length data");
    diag ("%s", ref);
    ok (blobref_hash ("sha1", data, sizeof (data), ref, sizeof (ref)) == 0,
        "blobref_hash sha1 works");
    diag ("%s", ref);

    ok (blobref_strtohash (ref, digest, sizeof (digest)) == SHA1_DIGEST_SIZE,
        "blobref_strtohash returns expected size hash");
    ok (blobref_hashtostr ("sha1", digest, SHA1_DIGEST_SIZE, ref2, sizeof (ref2)) == 0,
        "blobref_hashtostr back again works");
    diag ("%s", ref2);
    ok (strcmp (ref, ref2) == 0,
        "and blobrefs match");

    /* sha256 */
    ok (blobref_hash ("sha256", NULL, 0, ref, sizeof (ref)) == 0,
        "blobref_hash sha256 handles zero length data");
    diag ("%s", ref);
    ok (blobref_hash ("sha256", data, sizeof (data), ref, sizeof (ref)) == 0,
        "blobref_hash sha256 works");
    diag ("%s", ref);

    ok (blobref_strtohash (ref, digest, sizeof (digest)) == SHA256_BLOCK_SIZE,
        "blobref_strtohash returns expected size hash");
    ok (blobref_hashtostr ("sha256", digest, SHA256_BLOCK_SIZE, ref2, sizeof (ref2)) == 0,
        "blobref_hashtostr back again works");
    diag ("%s", ref2);
    ok (strcmp (ref, ref2) == 0,
        "and blobrefs match");

    /* blobref_validate */
    const char **pp;
    pp = &goodref[0];
    while (*pp) {
        ok (blobref_validate (*pp) == 0,
            "blobref_validate: %s", *pp);
        pp++;
    }
    pp = &badref[0];
    while (*pp) {
        errno = 0;
        ok (blobref_validate (*pp) < 0 && errno == EINVAL,
            "blobref_validate not: %s", *pp);
        pp++;
    }

    /* blobref_validate_hashtype */
    ok (blobref_validate_hashtype ("sha1") == 0,
        "blobref_validate_hashtype sha1 is valid");
    ok (blobref_validate_hashtype ("sha256") == 0,
        "blobref_validate_hashtype sha256 is valid");
    ok (blobref_validate_hashtype ("nerf") == -1,
        "blobref_validate_hashtype nerf is invalid");
    ok (blobref_validate_hashtype (NULL) == -1,
        "blobref_validate_hashtype NULL is invalid");

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
