/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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

#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "bizcard.h"

bool streq_safe (const char *s1, const char *s2)
{
    if (!s1 && !s2)
        return true;
    if (!s1 || !s1)
        return false;
    return streq (s1, s2);
}

bool test_bizcard_equiv (const struct bizcard *bc1,
                         const struct bizcard *bc2)
{
    if (!streq (bizcard_hostname (bc1), bizcard_hostname (bc2)))
        return false;
    if (bizcard_pubkey (bc1) == NULL || bizcard_pubkey (bc2) == NULL) {
        if (bizcard_pubkey (bc1) != bizcard_pubkey (bc2))
            return false;
    }
    else {
        if (!streq (bizcard_pubkey (bc1), bizcard_pubkey (bc2)))
            return false;
    }
    if (!streq_safe (bizcard_uri_first (bc1), bizcard_uri_first (bc2)))
        return false;
    do {
        if (!streq_safe (bizcard_uri_next (bc1), bizcard_uri_next (bc2)))
            return false;
    } while (bizcard_uri_next (bc1) != NULL);
    return true;
}

void test_simple (void)
{
    struct bizcard *bc;
    struct bizcard *bc2;
    const char *s;

    ok ((bc = bizcard_create ("hostname", "pubkey")) != NULL,
        "bizcard_create works");
    ok ((s = bizcard_hostname (bc)) != NULL
        && streq (s, "hostname"),
        "bizcard_hostname works");
    ok ((s = bizcard_pubkey (bc)) != NULL
        && streq (s, "pubkey"),
        "bizcard_pubkey works");
    ok (bizcard_uri_first (bc) == NULL,
        "bizcard_uri_first returns NULL");
    ok (bizcard_uri_next (bc) == NULL,
        "bizcard_uri_next returns NULL");
    ok (bizcard_uri_find (bc, NULL) == NULL,
        "bizcard_uri_find scheme=NULL returns NULL");
    ok (bizcard_uri_find (bc, "ipc") == NULL,
        "bizcard_uri_find scheme=ipc returns NULL");

    ok (bizcard_uri_append (bc, "ipc:///foo/bar") == 0,
        "bizcard_uri_append uri=ipc:///foo/bar works");
    ok ((s = bizcard_uri_first (bc)) != NULL
        && streq (s, "ipc:///foo/bar"),
        "bizcard_uri_first returns URI");
    ok (bizcard_uri_next (bc) == NULL,
        "bizcard_uri_next returns NULL");
    ok ((s = bizcard_uri_find (bc, NULL)) != NULL
        && streq (s, "ipc:///foo/bar"),
        "bizcard_uri_find scheme=NULL returns URI");
    ok ((s = bizcard_uri_find (bc, "ipc://")) != NULL
        && streq (s, "ipc:///foo/bar"),
        "bizcard_uri_find scheme=ipc:// returns URI");
    ok (bizcard_uri_find (bc, "tcp://") == NULL,
        "bizcard_uri_find scheme=tcp:// returns NULL");

    ok (bizcard_uri_append (bc, "tcp://192.168.1.1:1234") == 0,
        "bizcard_uri_append uri=tcp://192.168.1.1:1234 works");
    ok ((s = bizcard_uri_first (bc)) != NULL
        && streq (s, "ipc:///foo/bar"),
        "bizcard_uri_first returns ipc URI");
    ok ((s = bizcard_uri_next (bc)) != NULL
        && streq (s, "tcp://192.168.1.1:1234"),
        "bizcard_uri_next returns tcp URI");
    ok (bizcard_uri_next (bc) == NULL,
        "bizcard_uri_next returns NULL");
    ok ((s = bizcard_uri_find (bc, "ipc://")) != NULL
        && streq (s, "ipc:///foo/bar"),
        "bizcard_uri_find scheme=ipc:// returns ipc URI");
    ok ((s = bizcard_uri_find (bc, "tcp://")) != NULL
        && streq (s, "tcp://192.168.1.1:1234"),
        "bizcard_uri_find scheme=tcp:// returns tcp URI");

    ok ((s = bizcard_encode (bc)) != NULL,
        "bizcard_encode works");
    ok ((bc2 = bizcard_decode (s, NULL)) != NULL,
        "bizcard_decode works");
    ok (test_bizcard_equiv (bc, bc2),
        "new bizcard is same as the old one");

    bizcard_incref (bc);
    bizcard_decref (bc);

    bizcard_decref (bc);
    bizcard_decref (bc2);
}

void test_nopubkey (void)
{
    struct bizcard *bc;
    struct bizcard *bc2;
    const char *s;

    ok ((bc = bizcard_create ("thishost", NULL)) != NULL,
        "bizcard_create pubkey=NULL works");

    ok (bizcard_pubkey (NULL) == NULL,
        "bizcard_pubkey returns NULL");

    ok ((s = bizcard_encode (bc)) != NULL,
        "bizcard_encode works");
    ok ((bc2 = bizcard_decode (s, NULL)) != NULL,
        "bizcard_decode works");
    ok (test_bizcard_equiv (bc, bc2),
        "new bizcard is same as the old one");

    bizcard_decref (bc2);
    bizcard_decref (bc);
}

void test_inval (void)
{
    flux_error_t error;
    struct bizcard *bc;

    errno = 0;
    ok (bizcard_create (NULL, "pubkey") == NULL && errno == EINVAL,
        "bizcard_create hostname=NULL fails with EINVAL");

    lives_ok ({bizcard_decref (NULL);},
              "bizcard_decref NULL doesn't crash");
    lives_ok ({bizcard_incref (NULL);},
              "bizcard_incref NULL doesn't crash");

    errno = 0;
    ok (bizcard_encode (NULL) == NULL && errno == EINVAL,
        "bizcard_encode NULL fails with EINVAL");
    errno = 0;

    error.text[0] = '\0';
    ok (bizcard_decode (NULL, &error) == NULL
        && errno == EINVAL
        && error.text[0] != '\0',
        "bizcard_decode NULL fails with EINVAL and sets error");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (bizcard_decode ("badinput", &error) == NULL
        && errno == EINVAL
        && error.text[0] != '\0',
        "bizcard_decode badinput fails with EINVAL and sets error");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (bizcard_decode ("{}", &error) == NULL
        && errno == EINVAL
        && error.text[0] != '\0',
        "bizcard_decode {} fails with EINVAL and sets error");
    diag ("%s", error.text);

    if (!(bc = bizcard_create ("foo", "bar")))
        BAIL_OUT ("bizcard_create failed");

    errno = 0;
    ok (bizcard_uri_append (NULL, "foo://bar") < 0 && errno == EINVAL,
        "bizcard_uri_append bc=NULL fails with EINVAL");
    errno = 0;
    ok (bizcard_uri_append (bc, NULL) < 0 && errno == EINVAL,
        "bizcard_uri_append uri=NULL fails with EINVAL");

    ok (bizcard_uri_first (NULL) == NULL,
        "bizcard_uri_first NULL returns NULL");
    ok (bizcard_uri_next (NULL) == NULL,
        "bizcard_uri_next NULL returns NULL");
    ok (bizcard_uri_find (NULL, NULL) == NULL,
        "bizcard_uri_next bc=NULL returns NULL");
    ok (bizcard_pubkey (NULL) == NULL,
        "bizcard_pubkey NULL returns NULL");
    ok (bizcard_hostname (NULL) == NULL,
        "bizcard_hostname NULL returns NULL");

    bizcard_decref (bc);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_simple ();
    test_nopubkey ();
    test_inval ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
