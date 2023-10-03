/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <stdbool.h>
#include <errno.h>
#include <zmq.h>

#include "tap.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "cert.h"

// valid keypair
#define PAIR1_PUB "FYFE.@650VuUqRGygAtG.RC$A<cid63q(WEnR+&y"
#define PAIR1_SEC "^Oto<5V$^d/V}kj6^Q[LRwdgAY5K3gE{gN2+1ox)"

// valid keypair, sec has an embedded # character
#define PAIR2_PUB "Viyg(M7U*Q6ZI=D6(q?]amgXrQ{[IQnEY{IF5*X)"
#define PAIR2_SEC "7F)3@>MGW.+)..qc)8R#lfL31*^QX<GXufgpVtbA"

// contains invalid Z85 char (space) in a 5-char chunk
//   see 0MQ RFC 32/Z85 (https://rfc.zeromq.org/spec/32/)
#define NOTZ85 "Vtb A"

struct test_vec {
    const char *name;
    char *input;
};

static struct test_vec goodvec[] = {
    {
        .name = "czmq zcert sample",
        .input =
    "#   ****  Generated on 2023-09-16 23:15:27 by CZMQ  ****\n"
    "#   ZeroMQ CURVE **Secret** Certificate\n"
    "#   DO NOT PROVIDE THIS FILE TO OTHER USERS nor change its permissions.\n"
    "\n"
    "metadata\n"
    "    name = \"picl0\"\n"
    "    keygen.czmq-version = \"4.2.1\"\n"
    "    keygen.sodium-version = \"1.0.18\"\n"
    "    keygen.flux-core-version = \"0.54.0\"\n"
    "    keygen.hostname = \"picl0\"\n"
    "    keygen.time = \"2023-09-16T23:15:27\"\n"
    "    keygen.userid = \"5588\"\n"
    "    keygen.zmq-version = \"4.3.4\"\n"
    "curve\n"
    "    public-key = \"8)TKx/<plQR>gO0.HCH/AsS3n[QeKMOy@}$)=GVu\"\n"
    "    secret-key = \"225YW{2q$:dqH]7cCbZW4a-}5Al/)0vkb>cE)o}Z\"\n"
    },
    {
         .name = "cert with blank lines",
         .input =
    "curve\n"
    " public-key = \"" PAIR1_PUB "\"\n"
    "\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    "\n"
    },
    {
         .name = "cert with indented inline comments",
         .input =
    "metadata\n"
    "	# comment \n"
    "curve\n"
    "# comment \n"
    " public-key = \"" PAIR1_PUB "\"\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
    {
         .name = "cert with # in z85 key",
         .input =
    "curve\n"
    " public-key = \"" PAIR2_PUB "\"\n"
    " secret-key = \"" PAIR2_SEC "\"\n"
    },
};

static struct test_vec badvec[] = {
    {
        .name = "empty input",
        .input = ""
    },
    {
        .name = "cert with missing curve section",
        .input =
    "metadata\n"
    },
    {
        .name = "cert with empty curve section",
        .input =
    "metadata\n"
    "curve\n"
    },
    {
        .name = "cert with extra section",
        .input =
    "metadata\n"
    "unknown\n"
    "curve\n"
    " public-key = \"" PAIR1_PUB "\"\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
    {
        .name = "cert with curve section indented",
        .input =
    " curve\n"
    " public-key = \"" PAIR1_PUB "\"\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
    {
        .name = "cert with keys not indented",
        .input =
    "curve\n"
    "public-key = \"" PAIR1_PUB "\"\n"
    "secret-key = \"" PAIR1_SEC "\"\n"
    },
    {
        .name = "cert with public key missing",
        .input =
    "curve\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
    {
        .name = "cert with secret key missing",
        .input =
    "curve\n"
    " public-key = \"" PAIR1_PUB "\"\n"
    },
    {
        .name = "cert with public key containing illegal Z85",
        .input =
    "curve\n"
    " public-key = \"" "FYFE.@650VuUqRGygAtG.RC$A<cid63q(WE" NOTZ85 "\"\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
    {
        .name = "cert with public missing end quote",
        .input =
    "metadata\n"
    "curve\n"
    " public-key = \"" PAIR1_PUB "\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
    {
        .name = "cert with public key too long",
        .input =
    "metadata\n"
    "curve\n"
    " public-key = \"" PAIR1_PUB PAIR1_PUB "\"\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
#if (ZMQ_VERSION >= ZMQ_MAKE_VERSION(4,2,1))
    {
        .name = "cert with mismatched keypair",
        .input =
    "metadata\n"
    "curve\n"
    " public-key = \"" "YYFE.@650VuUqRGygAtG.RC$A<cid63q(WEnR+&y" "\"\n"
    " secret-key = \"" PAIR1_SEC "\"\n"
    },
#endif
};

void test_basic (void)
{
    struct cert *cert;
    struct cert *cert2;
    size_t memsize = 8182;
    void *membuf;
    FILE *f;
    const char *val;

    cert = cert_create ();
    ok (cert != NULL,
        "cert_create works");
    ok (cert_meta_set (cert, "foo", "bar") == 0,
        "cert_meta_set foo=bar works");
    val = cert_meta_get (cert, "foo");
    ok (val != NULL && streq (val, "bar"),
        "cert_meta_get foo returns bar");
    ok (cert_meta_get (cert, "notakey") == NULL,
        "cert_meta_get notakey returns NULL");

    if (!(membuf = calloc (1, memsize)))
        BAIL_OUT ("out of memory");
    if (!(f = fmemopen (membuf, memsize, "r+")))
        BAIL_OUT ("fmemopen failed: %s", strerror (errno));
    ok (cert_write (cert, f) == 0,
        "cert_write works");

    if (fseek (f, 0L, SEEK_SET) < 0)
        BAIL_OUT ("fseek 0 failed");
    cert2 = cert_read (f);
    ok (cert2 != NULL,
        "cert_read works");
    ok (cert_equal (cert, cert2),
        "cert_equal says we read back what we wrote");
    cert_destroy (cert2);

    ok ((cert2 = cert_create ()) != NULL,
        "cert_create created a second cert");
    ok (!cert_equal (cert, cert2),
        "cert_equal says it is different from the first cert");
    cert_destroy (cert2);

    fclose (f);
    cert_destroy (cert);

    cert = cert_create_from (NULL, NULL);
    ok (cert != NULL,
        "cert_create_from pub=NULL sec=NULL works");
    ok (cert_public_txt (cert) == NULL,
        "cert_public_txt returns NULL");
    ok (cert_secret_txt (cert) == NULL,
        "cert_secret_txt returns NULL");
    cert_destroy (cert);

    cert = cert_create_from (PAIR1_PUB, PAIR1_SEC);
    ok (cert != NULL,
        "cert_create_from works");
    ok (cert_public_txt (cert) != NULL
        && streq (cert_public_txt (cert), PAIR1_PUB),
        "cert_public_txt is the public key");
    ok (cert_secret_txt (cert) != NULL
        && streq (cert_secret_txt (cert), PAIR1_SEC),
        "cert_secret_txt is the secret key");
    struct cert *cpub;
    struct cert *csec;
    cpub = cert_create_from (PAIR1_PUB, NULL);
    ok (cpub != NULL,
        "cert_create_from sec=NULL works");
    ok (cert_public_txt (cpub) != NULL
        && streq (cert_public_txt (cpub), PAIR1_PUB),
        "cert_public_txt is the public key");
    ok (cert_secret_txt (cpub) == NULL,
        "cert_secret_txt returns NULL");
    csec = cert_create_from (NULL, PAIR1_SEC);
    ok (csec != NULL,
        "cert_create_from pub=NULL works");
    ok (cert_public_txt (csec) == NULL,
        "cert_public_txt returns NULL");
    ok (cert_secret_txt (csec) != NULL
        && streq (cert_secret_txt (csec), PAIR1_SEC),
        "cert_secret_txt is the secret key");
    ok (!cert_equal (cert, cpub)
        && !cert_equal (cert, csec)
        && !cert_equal (cpub, csec),
        "cert_equal handles partial certs OK");
    cert_destroy (cpub);
    cert_destroy (csec);
    cert_destroy (cert);


    free (membuf);
}

bool test_good_one (char *s)
{
    FILE *f_in;
    FILE *f_inout = NULL;
    struct cert *cert = NULL;
    struct cert *cert2 = NULL;
    bool result = false;
    size_t memsize = 8182;
    void *membuf = NULL;

    /* read cert from 's' */
    if (!(f_in = fmemopen (s, strlen (s) + 1, "r")))
        BAIL_OUT ("fmemopen of good input failed: %s", strerror (errno));
    if (!(cert = cert_read (f_in))) {
        diag ("cert_read input failed\n");
        goto done;
    }

    /* write cert to file */
    if (!(membuf = calloc (1, memsize)))
        BAIL_OUT ("out of memory");
    if (!(f_inout = fmemopen (membuf, memsize, "w+")))
        BAIL_OUT ("fmemopen failed: %s", strerror (errno));
    if (cert_write (cert, f_inout) < 0) {
        diag ("cert_write failed");
        goto done;
    }

    if (fseek (f_inout, 0L, SEEK_SET) < 0)
        BAIL_OUT ("fseek 0 failed");

    /* read cert from file */
    if (!(cert2 = cert_read (f_inout))) {
        diag ("cert_read tmp failed");
        goto done;
    }

    /* compare certs */
    result = cert_equal (cert, cert2);

done:
    if (f_in)
        fclose (f_in);
    if (f_inout)
        fclose (f_inout);
    free (membuf);
    cert_destroy (cert);
    cert_destroy (cert2);
    return result;
}

bool test_bad_one (char *s)
{
    FILE *f;
    struct cert *cert;

    if (!(f = fmemopen (s, strlen (s) + 1, "r")))
        BAIL_OUT ("fmemopen of bad input failed: %s", strerror (errno));
    cert = cert_read (f);
    fclose (f);
    if (cert) {
        diag ("cert_read unexpectedly succeeded\n");
        cert_destroy (cert);
        return false;
    }
    return true;
}

void test_vec (void)
{
    for (int i = 0; i < ARRAY_SIZE (goodvec); i++) {
        ok (test_good_one (goodvec[i].input),
           "%s can be read/written/read", goodvec[i].name);
    }
    for (int i = 0; i < ARRAY_SIZE (badvec); i++) {
        ok (test_bad_one (badvec[i].input),
           "%s fails as expected", badvec[i].name);
    }
}

void test_inval (void)
{
    struct cert *cert;
    struct cert *cpub;

    if (!(cert = cert_create ()))
        BAIL_OUT ("could not create cert");
    if (!(cpub = cert_create_from (cert_public_txt (cert), NULL)))
        BAIL_OUT ("could not create cert");

    errno = 0;
    ok (cert_meta_set (NULL, "foo", "bar") == -1 && errno == EINVAL,
        "cert_meta_set cert=NULL fails with EINVAL");
    errno = 0;
    ok (cert_meta_set (cert, NULL, "bar") == -1 && errno == EINVAL,
        "cert_meta_set key=NULL fails with EINVAL");
    errno = 0;
    ok (cert_meta_set (cert, "", "bar") == -1 && errno == EINVAL,
        "cert_meta_set key=\"\" fails with EINVAL");
    errno = 0;
    ok (cert_meta_set (cert, "foo", NULL) == -1 && errno == EINVAL,
        "cert_meta_set val=NULL fails with EINVAL");

    const char *shortkey = "S:do@!Xbon>XQ$e!SK";
    errno = 0;
    ok (cert_create_from (shortkey, NULL) == NULL && errno == EINVAL,
        "cert_create_from pub=shortkey fails with EINVAL");
    errno = 0;
    ok (cert_create_from (NULL, shortkey) == NULL && errno == EINVAL,
        "cert_create_from sec=shortkey fails with EINVAL");

    const char *badz85 = "s70JW1{s!)bET!S&yF=7z=b{%+<2Nu1zO31tCad\002";
    errno = 0;
    ok (cert_create_from (badz85, NULL) == NULL && errno == EINVAL,
        "cert_create_from pub=badz85 fails with EINVAL");
    errno = 0;
    ok (cert_create_from (NULL, badz85) == NULL && errno == EINVAL,
        "cert_create_from sec=badz85 fails with EINVAL");

    errno = 0;
    ok (cert_write (NULL, stderr) < 0 && errno == EINVAL,
        "cert_write cert=NULL fails with EINVAL");
    errno = 0;
    ok (cert_write (cpub, stderr) < 0 && errno == EINVAL,
        "cert_write cert=NULL fails with EINVAL");
    errno = 0;
    ok (cert_write (cert, NULL) < 0 && errno == EINVAL,
        "cert_write f=NULL fails with EINVAL");
    errno = 0;
    ok (cert_write (cpub, stderr) < 0 && errno == EINVAL,
        "cert_write cert=partial fails with EINVAL");

    lives_ok ({cert_destroy (NULL);},
              "cert_destroy cert=NULL does not crash");

    ok (cert_public_txt (NULL) == NULL,
        "cert_public_txt cert=NULL returns NULL");
    ok (cert_secret_txt (NULL) == NULL,
        "cert_secret_txt cert=NULL returns NULL");

    errno = EINVAL;
    ok (cert_apply (NULL, NULL) < 0 && errno == EINVAL,
        "cert_apply cert=NULL fails with EINVAL");
    errno = EINVAL;
    ok (cert_apply (cpub, NULL) < 0 && errno == EINVAL,
        "cert_apply cert=partial fails with EINVAL");
    errno = EINVAL;
    ok (cert_apply (cert, NULL) < 0 && errno == ENOTSOCK,
        "cert_apply sock=NULL fails with ENOTSOCK");

    cert_destroy (cpub);
    cert_destroy (cert);
}


int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_vec ();
    test_inval ();

    done_testing();

    return 0;
}

// vi:ts=4 sw=4 expandtab
