/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "src/common/libtap/tap.h"
#include "src/common/libidset/idset.h"
#include "src/common/libidset/idset_private.h"

struct inout {
    const char *in;
    int flags;
    const char *out;
};

struct inout test_inputs[] = {
    { "2",              0,          "2" },
    { "7-9",            0,          "7,8,9" },
    { "9-7",            0,          "7,8,9" },
    { "1,7-9",          0,          "1,7,8,9" },
    { "1,7-9,16",       0,          "1,7,8,9,16" },
    { "1,7-9,14,16",    0,          "1,7,8,9,14,16" },
    { "1-3,7-9,14,16",  0,          "1,2,3,7,8,9,14,16" },
    { "3,2,4,5",        0,          "2,3,4,5" },
    { "",               0,          ""},
    { "1048576",        0,          "1048576"},

    { "[2]",            0,          "2" },
    { "[7-9]",          0,          "7,8,9" },
    { "[9-7]",          0,          "7,8,9" },
    { "[3,2,4,5]",      0,          "2,3,4,5" },
    { "[]",             0,          ""},

    { "2",              IDSET_FLAG_RANGE,  "2" },
    { "7-9",            IDSET_FLAG_RANGE,  "7-9" },
    { "9-7",            IDSET_FLAG_RANGE,  "7-9" },
    { "1,7-9",          IDSET_FLAG_RANGE,  "1,7-9" },
    { "1,7-9,16",       IDSET_FLAG_RANGE,  "1,7-9,16" },
    { "1,7-9,14,16",    IDSET_FLAG_RANGE,  "1,7-9,14,16" },
    { "1-3,7-9,14,16",  IDSET_FLAG_RANGE,  "1-3,7-9,14,16" },
    { "3,2,4,5",        IDSET_FLAG_RANGE,  "2-5" },
    { "",               IDSET_FLAG_RANGE,  ""},

    { "2",             IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "2" },
    { "7-9",           IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[7-9]" },
    { "9-7",           IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[7-9]" },
    { "1,7-9",         IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9]" },
    { "1,7-9,16",      IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9,16]" },
    { "1,7-9,14,16",   IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9,14,16]" },
    { "1-3,7-9,14,16", IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1-3,7-9,14,16]"},
    { "3,2,4,5",       IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[2-5]" },
    { "",              IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, ""},

    { NULL, 0, NULL },
};

void test_basic (void)
{
    struct idset *idset;

    idset = idset_create (0, 0);
    ok (idset != NULL,
        "idset_create size=0 works");

    idset_destroy (idset);
}

void test_codec (void)
{
    struct inout *ip;

    for (ip = &test_inputs[0]; ip->in != NULL; ip++) {
        struct idset *idset;
        char *s;

        idset = idset_decode (ip->in);
        ok (idset != NULL,
            "idset_decode '%s' works", ip->in);
        s = idset_encode (idset, ip->flags);
        bool match = (s == NULL && ip->out == NULL)
                  || (s && ip->out && !strcmp (s, ip->out));
        ok (match == true,
            "idset_encode flags=0x%x '%s' works",
            ip->flags, ip->out ? ip->out : "NULL");
        if (!match)
            diag ("%s", s ? s : "NULL");
        free (s);
        idset_destroy (idset);
    }
}

/* Try a big one to cover encode buffer growth */
void test_codec_large (void)
{
    struct idset *idset;
    char *s;

    idset = idset_decode ("0-5000");
    ok (idset != NULL,
        "idset_decode '0-5000' works");
    s = idset_encode (idset, 0);
    int count = 0;
    if (s) {
        char *a1 = s;
        char *tok;
        char *saveptr;
        while ((tok = strtok_r (a1, ",", &saveptr))) {
            int i = strtol (tok, NULL, 10);
            if (i != count)
                break;
            count++;
            a1 = NULL;
        }
    }
    ok (count == 5001,
        "idset_encode flags=0x0 '0,2,3,...,5000' works");
    if (count != 5001)
        diag ("count=%d", count);
    free (s);
    idset_destroy (idset);
}

void test_badparam (void)
{
    struct idset *idset;

    if (!(idset = idset_create (100, 0)))
        BAIL_OUT ("idset_create failed");

    errno = 0;
    ok (idset_create (1000, IDSET_FLAG_BRACKETS) == NULL && errno == EINVAL,
        "idset_create(flags=wrong) fails with EINVAL");

    errno = 0;
    ok (idset_encode (NULL, 0) == NULL && errno == EINVAL,
        "idset_encode(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_encode (idset, IDSET_FLAG_AUTOGROW) == NULL && errno == EINVAL,
        "idset_encode(flags=wrong) fails with EINVAL");
    errno = 0;
    ok (idset_decode (NULL) == NULL && errno == EINVAL,
        "idset_decode(s=NULL) fails with EINVAL");

    errno = 0;
    ok (idset_set (NULL, 1) < 0 && errno == EINVAL,
        "iset_set(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_set (idset, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "iset_set(id=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_set (idset, 101) < 0 && errno == EINVAL,
        "iset_set(id=out of range) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (NULL, 1, 2) < 0 && errno == EINVAL,
        "iset_range_set(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, 1, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "iset_range_set(hi=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, IDSET_INVALID_ID, 1) < 0 && errno == EINVAL,
        "iset_range_set(lo=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, 101, 1) < 0 && errno == EINVAL,
        "iset_range_set(lo=out of range) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, 1, 101) < 0 && errno == EINVAL,
        "iset_range_set(hi=out of range) fails with EINVAL");

    errno = 0;
    ok (idset_clear (NULL, 1) < 0 && errno == EINVAL,
        "iset_clear(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_clear (idset, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "iset_clear(id=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_clear (idset, 101) == 0,
        "iset_clear(id=out of range) works");
    errno = 0;
    ok (idset_range_clear (NULL, 1, 2) < 0 && errno == EINVAL,
        "iset_range_clear(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_range_clear (idset, 1, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "iset_range_clear(hi=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_range_clear (idset, IDSET_INVALID_ID, 1) < 0 && errno == EINVAL,
        "iset_range_clear(lo=INVALID) fails with EINVAL");

    ok (idset_test (NULL, 1) == false,
        "iset_test(idset=NULL) returns false");

    ok (idset_count (NULL) == 0,
        "iset_count(idset=NULL) returns 0");

    errno = 0;
    ok (idset_copy (NULL) == NULL && errno == EINVAL,
        "iset_copy(idset=NULL) fails with EINVAL");

    ok (idset_first (NULL) == IDSET_INVALID_ID,
        "idset_first (idset=NULL) returns IDSET_INVALID_ID");
    ok (idset_next (NULL, 0) == IDSET_INVALID_ID,
        "idset_next (idset=NULL) returns IDSET_INVALID_ID");
    ok (idset_next (idset, IDSET_INVALID_ID) == IDSET_INVALID_ID,
        "idset_next (prev=INVALID) returns IDSET_INVALID_ID");
    ok (idset_next (idset, 101) == IDSET_INVALID_ID,
        "idset_next (prev=out of range) returns IDSET_INVALID_ID");
    ok (idset_last (NULL) == IDSET_INVALID_ID,
        "idset_last (idset=NULL) returns IDSET_INVALID_ID");

    idset_destroy (idset);
}

void test_iter (void)
{
    struct idset *idset;
    struct idset *idset_empty;

    if (!(idset = idset_decode ("7-9")))
        BAIL_OUT ("idset_decode 7-9 failed");
    if (!(idset_empty = idset_create (0, 0)))
        BAIL_OUT ("idset_create (0, 0) failed");

    ok (idset_first (idset) == 7,
        "idset_first idset=[7-9] returned 7");
    ok (idset_next (idset, 7) == 8,
        "idset_next idset=[7-9] prev=7 returned 8");
    ok (idset_next (idset, 8) == 9,
        "idset_next idset=[7-9] prev=8 returned 9");
    ok (idset_next (idset, 9) == IDSET_INVALID_ID,
        "idset_next idset=[7-9] prev=9 returned INVALID");
    ok (idset_next (idset, 10) == IDSET_INVALID_ID,
        "idset_next idset=[7-9] prev=10 returned INVALID");
    ok (idset_next (idset, 4096) == IDSET_INVALID_ID,
        "idset_next idset=[7-9] prev=4096 returned INVALID");
    ok (idset_next (idset, IDSET_INVALID_ID) == IDSET_INVALID_ID,
        "idset_next idset=[7-9] prev=INVALID returned INVALID");
    ok (idset_last (idset) == 9,
        "idset_last idset=[7-9] returned 9");

    ok (idset_first (idset_empty) == IDSET_INVALID_ID,
        "idset_first idset=[] returned IDSET_INVALID_ID");
    ok (idset_last (idset_empty) == IDSET_INVALID_ID,
        "idset_last idset=[] returned IDSET_INVALID_ID");
    ok (idset_next (idset_empty, 0) == IDSET_INVALID_ID,
        "idset_next idset=[] prev=0 returned IDSET_INVALID_ID");

    idset_destroy (idset);
    idset_destroy (idset_empty);
}

void test_set (void)
{
    struct idset *idset;

    if (!(idset = idset_create (100, 0)))
        BAIL_OUT ("idset_create failed");

    ok (idset_count (idset) == 0,
        "idset_count (idset) == 0");
    ok (idset_set (idset, 0) == 0,
        "idset_set 0 worked");
    ok (idset_count (idset) == 1,
        "idset_count (idset) == 1");
    ok (idset_set (idset, 0) == 0,
        "idset_set 0 again  succeeds");
    ok (idset_count (idset) == 1,
        "idset_count (idset) == 1");
    ok (idset_set (idset, 3) == 0,
        "idset_set 3 worked");
    ok (idset_set (idset, 99) == 0,
        "idset_set 99 worked");
    errno = 0;
    ok (idset_set (idset, 100) < 0 && errno == EINVAL,
        "idset_set id=size and no autogrow failed with EINVAL");
    errno = 0;
    ok (idset_set (idset, UINT_MAX) < 0 && errno == EINVAL,
        "idset_set id=UINT_MAX failed with EINVAL");
    errno = 0;
    ok (idset_set (idset, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "idset_set id=INVALID failed with EINVAL");

    ok (idset_first (idset) == 0,
        "idset_first returned 0");
    ok (idset_next (idset, 0) == 3,
        "idset_next prev=0 returned 3");
    ok (idset_next (idset, 3) == 99,
        "idset_next prev=3 returned 99");
    ok (idset_next (idset, 99) == IDSET_INVALID_ID,
        "idset_next prev=99 returned INVALID");

    idset_destroy (idset);
}

void test_range_set (void)
{
    struct idset *idset;

    if (!(idset = idset_create (100, 0)))
        BAIL_OUT ("idset_create failed");

    ok (idset_range_set (idset, 0, 2) == 0,
        "idset_range_set 0-2 worked");
    ok (idset_count (idset) == 3,
        "idset_count == 3");
    ok (idset_range_set (idset, 0, 2) == 0,
        "idset_range_set 0-2 again worked");
    ok (idset_count (idset) == 3,
        "idset_count == 3");
    ok (idset_range_set (idset, 80, 79) == 0, // reversed
        "idset_set 80-79 worked");
    ok (idset_count (idset) == 5,
        "idset_count == 5");

    errno = 0;
    ok (idset_range_set (idset, 100, 101) < 0 && errno == EINVAL,
        "idset_range_set size-(size+1) and no autogrow failed with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, UINT_MAX, UINT_MAX-1) < 0 && errno == EINVAL,
        "idset_range_set id=UINT_MAX-(UNIT_MAX-1) failed with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, IDSET_INVALID_ID, IDSET_INVALID_ID+1) < 0 && errno == EINVAL,
        "idset_set id=INVALID-(INVALID+1) failed with EINVAL");

    ok (idset_first (idset) == 0,
        "idset_first returned 0");
    ok (idset_next (idset, 0) == 1,
        "idset_next prev=0 returned 1");
    ok (idset_next (idset, 1) == 2,
        "idset_next prev=1 returned 2");
    ok (idset_next (idset, 2) == 79,
        "idset_next prev=2 returned 79");
    ok (idset_next (idset, 79) == 80,
        "idset_next prev=2 returned 80");
    ok (idset_next (idset, 80) == IDSET_INVALID_ID,
        "idset_next prev=80 returned INVALID");

    idset_destroy (idset);
}

void test_clear (void)
{
    struct idset *idset;
    unsigned int id;

    if (!(idset = idset_decode ("1-10")))
        BAIL_OUT ("idset_decode [1-10] failed");

    ok (idset_count (idset) == 10,
        "idset_count [1-10] returns 10");
    for (id = 1; id <= 7; id++) {
        ok (idset_test (idset, id) == true,
            "idset_test %d initially true", id);
        ok (idset_clear (idset, id) == 0,
            "idset_clear idset=[%d-10], id=%d worked", id, id);
        ok (idset_test (idset, id) == false,
            "idset_test %d is now false", id);
    }
    ok (idset_count (idset) == 3,
        "idset_count returns 3");

    ok (idset_clear (idset, 100) == 0,
        "idset_clear idset=[8-10], id=100 works");
    ok (idset_count (idset) == 3,
        "idset_count still returns 3");
    errno = 0;
    ok (idset_clear (idset, UINT_MAX) < 0 && errno == EINVAL,
        "idset_clear idset=[8-10], id=UINT_MAX failed with EINVAL");
    errno = 0;
    ok (idset_clear (idset, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "idset_clear idset=[8-10], id=INVALID failed with EINVAL");

    ok (idset_first (idset) == 8,
        "idset_first idset=[8-10] returned 8");
    ok (idset_next (idset, 8) == 9,
        "idset_next idset=[8-10], prev=8 returned 9");
    ok (idset_next (idset, 9) == 10,
        "idset_next idset=[8-10], prev=9 returned 10");
    ok (idset_next (idset, 10) == IDSET_INVALID_ID,
        "idset_next idset=[8-10], prev=10 returned INVALID");

    idset_destroy (idset);
}

void test_range_clear (void)
{
    struct idset *idset;

    if (!(idset = idset_decode ("1-10")))
        BAIL_OUT ("idset_decode [1-10] failed");

    ok (idset_range_clear (idset, 2, 5) == 0,
        "idset_range_clear 2-5 works");
    ok (idset_count (idset) == 6,
        "idset_count == 6");
    ok (idset_range_clear (idset, 2, 5) == 0,
        "idset_range_clear 2-5 again succeeds");
    ok (idset_count (idset) == 6,
        "idset_count is still 6");
    ok (idset_range_clear (idset, 9, 6) == 0, // reversed
        "idset_range_clear 9-6 works");
    errno = 0;
    ok (idset_range_clear (idset, IDSET_INVALID_ID, 2) < 0 && errno == EINVAL,
        "idset_range_clear lo=INVALID  fails with EINVAL");
    errno = 0;
    ok (idset_range_clear (idset, 2, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "idset_range_clear hi=INVALID  fails with EINVAL");

    ok (idset_first (idset) == 1,
        "idset_first returned 1");
    ok (idset_next (idset, 1) == 10,
        "idset_next prev=1 returned 10");
    ok (idset_next (idset, 10) == IDSET_INVALID_ID,
        "idset_next prev=10 returned INVALID");

    idset_destroy (idset);
}

void test_equal (void)
{
    struct idset *set1 = NULL;
    struct idset *set2 = NULL;

    ok (idset_equal (set1, set2) == false,
        "idset_equal (NULL, NULL) == false");

    if (!(set1 = idset_decode ("1-10")))
        BAIL_OUT ("idset_decode [1-10] failed");
    ok (idset_equal (set1, set2) == false,
        "idset_equal (set1, NULL) == false");

    if (!(set2 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create (1024, 0) failed");
    ok (idset_equal (set1, set2) == false,
        "idset_equal returns false");
    ok (idset_range_set (set2, 0, 9) == 0,
        "idset_range_set (set2, 0, 9) succeeds");
    ok (idset_equal (set1, set2) == false,
        "idset_equal of non-equal but equivalent size sets returns false");
    ok (idset_set (set2, 10) == 0 && idset_clear (set2, 0) == 0,
        "idset_set (set2, 10) && idset_clear (set2, 0)");
    ok (idset_equal (set1, set2),
        "idset_equal (set1, set2) == true");

    ok (idset_range_clear (set1, 1, 10) == 0 &&
        idset_range_clear (set2, 1, 10) == 0,
        "idset_clear all entries from set1 and set2");
    ok (idset_count (set1) == 0 && idset_count (set2) == 0,
        "idset_count (set1) == idset_count (set2) == 0");
    ok (idset_equal (set1, set2) == true,
        "idset_equal returns true for two empty sets");

    idset_destroy (set1);
    idset_destroy (set2);
}

void test_copy (void)
{
    struct idset *idset;
    struct idset *cpy;

    if (!(idset = idset_decode ("1-5000")))
        BAIL_OUT ("idset_decode [1-5000] failed");

    ok (idset_count (idset) == 5000,
        "idset_count idset=[1-5000] returns 5000");
    ok ((cpy = idset_copy (idset)) != NULL,
        "idset_copy made a copy");
    ok (idset_count (cpy) == 5000,
        "idset_count on copy returns 5000");
    ok (idset_equal (idset, cpy) == true,
        "idset_copy made an accurate copy");
    ok (idset_clear (cpy, 100) == 0,
        "idset_clear 100 on copy");
    ok (idset_count (cpy) == 4999,
        "idset_count on copy returns 4999");
    ok (idset_count (idset) == 5000,
        "idset_count on orig returns 5000");
    idset_destroy (cpy);

    idset_destroy (idset);
}

void test_autogrow (void)
{
    struct idset *idset;

    idset = idset_create (1, 0);
    ok (idset != NULL,
        "idset_create size=1 flags=0 works");
    ok (idset->T.M == 1,
        "idset internal size is 1");
    ok (idset_set (idset, 0) == 0,
        "idset_set 0 works");
    errno = 0;
    ok (idset_set (idset, 1) < 0 && errno == EINVAL,
        "idset_set 1 fails with EINVAL");
    idset_destroy (idset);

    idset = idset_create (1, IDSET_FLAG_AUTOGROW);
    ok (idset != NULL,
        "idset_create size=1 flags=AUTOGROW works");
    ok (idset->T.M == 1,
        "idset internal size is 1");
    ok (idset_set (idset, 0) == 0,
        "idset_set 0 works");
    ok (idset_set (idset, 2) == 0,
        "idset_set 2 works");
    ok (idset->T.M > 1,
        "idset internal size grew");
    ok (   idset_test (idset, 0)
        && !idset_test (idset, 1)
        && idset_test (idset, 2)
        && !idset_test (idset, 3),
        "idset contains expected ids");
    idset_destroy (idset);
}

void issue_1974(void)
{
    struct idset *idset;

    idset = idset_create (1024, 0);
    ok (idset != NULL,
        "1974: idset_create size=1024 worked");
    ok (idset_test (idset, 1024) == false,
        "1974: idset_test id=1024 returned false");
    idset_destroy (idset);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_badparam ();
    test_codec ();
    test_codec_large ();
    test_iter ();
    test_set ();
    test_range_set ();
    test_clear ();
    test_range_clear ();
    test_equal ();
    test_copy ();
    test_autogrow ();
    issue_1974 ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
