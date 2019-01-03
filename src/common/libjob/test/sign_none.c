/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>
#include <sodium.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjob/sign_none.h"

void simple (void)
{
    uint32_t userid;
    void *payload;
    int payloadsz;
    char *s;
    int rc;

    s = sign_none_wrap ("foo", 4, 1000);
    if (!s)
        BAIL_OUT ("sign_none_wrap returned NULL");
    ok (s != NULL,
        "sign_none_wrap works");
    diag (s);

    userid = 0;
    payload = NULL;
    payloadsz = 0;
    rc = sign_none_unwrap (s, &payload, &payloadsz, &userid);
    ok (rc == 0 && userid == 1000
        && payloadsz == 4 && memcmp (payload, "foo", 4) == 0,
        "sign_none_unwrap works");

    free (s);
}

char *encode_base64 (const void *src, int srclen)
{
    int dstlen = sodium_base64_encoded_len (srclen,
                                            sodium_base64_VARIANT_ORIGINAL);
    char *dst = calloc (1, dstlen);
    if (!dst)
        BAIL_OUT ("calloc failed");
    return sodium_bin2base64 (dst, dstlen, (unsigned char *)src, srclen,
                              sodium_base64_VARIANT_ORIGINAL);
}

char *wrap (const char *header, int headerlen, void *payload, int payloadlen)
{
    char *h = encode_base64 (header, headerlen);
    char *p = encode_base64 (payload, payloadlen);
    char *result;
    if (asprintf (&result, "%s.%s.none", h, p) < 0)
        BAIL_OUT ("asprintf failed");
    free (h);
    free (p);
    return result;
}

/* Try header kv's in different orders, and possible corner cases
 * on payload and userid.
 */
void decode_good (void)
{
    const char good1_header[] = "version\0i1\0userid\0i1000\0mech\0snone";
    char *good1 = wrap (good1_header, sizeof (good1_header), "foo", 4);
    const char good2_header[] = "userid\0i1000\0mech\0snone\0version\0i1";
    char *good2 = wrap (good2_header, sizeof (good2_header), NULL, 0);
    const char good3_header[] = "mech\0snone\0version\0i1\0userid\0i0";
    char *good3 = wrap (good3_header, sizeof (good3_header), "", 1);

    uint32_t userid;
    void *payload;
    int payloadsz;
    int rc;

    userid = 0;
    payload = NULL;
    payloadsz = 0;
    diag ("test 1: %s", good1);
    rc = sign_none_unwrap (good1, &payload, &payloadsz, &userid);
    ok (rc == 0 && userid == 1000
        && payloadsz == 4 && memcmp (payload, "foo", 4) == 0,
        "dummy encode 1 decodes as expected");

    userid = 0;
    payload = NULL;
    payloadsz = 0;
    diag ("test 2: %s", good2);
    rc = sign_none_unwrap (good2, &payload, &payloadsz, &userid);
    ok (rc == 0 && userid == 1000 && payloadsz == 0,
        "dummy encode 2 decodes as expected");

    userid = 1;
    payload = NULL;
    payloadsz = 0;
    diag ("test 3: %s", good3);
    rc = sign_none_unwrap (good3, &payload, &payloadsz, &userid);
    ok (rc == 0 && userid == 0 && payloadsz == 1 && *(char *)payload == '\0',
        "dummy encode 3 decodes as expected");

    free (good1);
    free (good2);
    free (good3);
}

void decode_bad_header (void)
{
    /* version 2 */
    const char bad1_header[] = "version\0i2\0userid\0i1000\0mech\0snone";
    char *bad1 = wrap (bad1_header, sizeof (bad1_header), NULL, 0);
    /* string version */
    const char bad2_header[] = "version\0s1\0userid\0i1000\0mech\0snone";
    char *bad2 = wrap (bad2_header, sizeof (bad2_header), NULL, 0);
    /* missing version */
    const char bad3_header[] = "userid\0i1000\0mech\0snone";
    char *bad3 = wrap (bad3_header, sizeof (bad3_header), NULL, 0);
    /* extra foo field */
    const char bad4_header[] = "foo\0i0\0version\0i1\0userid\0i1000\0mech\0snone";
    char *bad4 = wrap (bad4_header, sizeof (bad4_header), NULL, 0);
    /* negative userid */
    const char bad5_header[] = "version\0i1\0userid\0i-1\0mech\0snone";
    char *bad5 = wrap (bad5_header, sizeof (bad5_header), NULL, 0);
    /* wrong type userid */
    const char bad6_header[] = "version\0i1\0userid\0s42\0mech\0snone";
    char *bad6 = wrap (bad6_header, sizeof (bad6_header), NULL, 0);
    /* missing userid */
    const char bad7_header[] = "version\0i1\0mech\0snone";
    char *bad7 = wrap (bad7_header, sizeof (bad7_header), NULL, 0);
    /* wrong mech */
    const char bad8_header[] = "version\0i1\0userid\0i1000\0mech\0smunge";
    char *bad8 = wrap (bad8_header, sizeof (bad8_header), NULL, 0);
    /* wrong type mech */
    const char bad9_header[] = "version\0i1\0userid\0i1000\0mech\0inone";
    char *bad9 = wrap (bad9_header, sizeof (bad9_header), NULL, 0);
    /* missing mech */
    const char bad10_header[] = "version\0i1\0userid\0i1000";
    char *bad10 = wrap (bad10_header, sizeof (bad10_header), NULL, 0);
    /* extra separator */
    const char bad11_header[] = "\0version\0i1\0userid\0i1000\0mech\0snone";
    char *bad11 = wrap (bad11_header, sizeof (bad11_header), NULL, 0);

    uint32_t userid;
    void *payload;
    int payloadsz;
    int rc;

    errno = 0;
    rc = sign_none_unwrap (bad1, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap bad header version fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad2, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap bad header version type fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad3, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap missing header version fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad4, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap extra header field fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad5, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap bad header userid value fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad6, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap bad header userid type fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad7, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap missing header userid fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad8, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap bad mech value fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad9, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap bad mech type fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad10, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap missing mech fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad11, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap extra seprator fails with EINVAL");

    free (bad1);
    free (bad2);
    free (bad3);
    free (bad4);
    free (bad5);
    free (bad6);
    free (bad7);
    free (bad8);
    free (bad9);
    free (bad10);
    free (bad11);
}

void decode_bad_other (void)
{
    const char *good = "dmVyc2lvbgBpMQB1c2VyaWQAaTEwMDAAbWVjaABzbm9uZQA=.Zm9vAA==.none";
    /* wrong suffix */
    const char *bad1  = "dmVyc2lvbgBpMQB1c2VyaWQAaTEwMDAAbWVjaABzbm9uZQA=.Zm9vAA==.wrong";
    /* missing field */
    const char *bad2  = "dmVyc2lvbgBpMQB1c2VyaWQAaTEwMDAAbWVjaABzbm9uZQA=.none";
    /* two missing fields */
    const char *bad3  = "none";
    /* invalid base64 payload (% character) */
    const char *bad4  = "dmVyc2lvbgBpMQB1c2VyaWQAaTEwMDAAbWVjaABzbm9uZQA=.%m9vAA==.none";
    /* invalid base64 header (% character) */
    const char *bad5  = "%mVyc2lvbgBpMQB1c2VyaWQAaTEwMDAAbWVjaABzbm9uZQA=.Zm9vAA==.none";

    uint32_t userid;
    void *payload;
    int payloadsz;
    int rc;

    /* Double check good input, the basis for bad input.
     */
    rc = sign_none_unwrap (good, &payload, &payloadsz, &userid);
    ok (rc == 0,
        "sign_none_unwrap baseline for bad input tests works");
    if (rc == 0)
        free (payload);

    errno = 0;
    rc = sign_none_unwrap (bad1, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap wrong suffix fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad2, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap missing field fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad3, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap two missing fields fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad4, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap invalid base64 payload fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (bad5, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap invalid base64 header fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap ("", &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap emtpy input fails with EINVAL");
}


void badarg (void)
{
    uint32_t userid;
    void *payload;
    int payloadsz;
    int rc;
    char *s;

    /* unwrap */

    const char good1_header[] = "version\0i1\0userid\0i1000\0mech\0snone";
    char *good1 = wrap (good1_header, sizeof (good1_header), "foo", 4);

    errno = 0;
    rc = sign_none_unwrap (NULL, &payload, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap input=NULL fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (good1, NULL, &payloadsz, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap payload=NULL fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (good1, &payload, NULL, &userid);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap payloadsz=NULL fails with EINVAL");

    errno = 0;
    rc = sign_none_unwrap (good1, &payload, &payloadsz, NULL);
    ok (rc < 0 && errno == EINVAL,
        "sign_none_unwrap userid=NULL fails with EINVAL");

    free (good1);

    /* wrap */

    errno = 0;
    s = sign_none_wrap (NULL, 4, 1000);
    ok (s == NULL && errno == EINVAL,
        "sign_none_wrap payload=NULL, payloadsz=4 fails with EINVAL");

    errno = 0;
    s = sign_none_wrap ("foo", -1, 1000);
    ok (s == NULL && errno == EINVAL,
        "sign_none_wrap payloadsz=-1 fails with EINVAL");

}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    simple ();
    decode_good ();
    decode_bad_header ();
    decode_bad_other ();
    badarg ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
