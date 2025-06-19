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
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "src/common/libtap/tap.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libidset/idset.h"
#include "src/common/libidset/idset_private.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

struct inout {
    const char *in;
    int flags;
    const char *out;
};

struct inout test_inputs[] = {
    { "2",              0,          "2" },
    { "7-9",            0,          "7,8,9" },
    { "1,7-9",          0,          "1,7,8,9" },
    { "1,7-9,16",       0,          "1,7,8,9,16" },
    { "1,7-9,14,16",    0,          "1,7,8,9,14,16" },
    { "1-3,7-9,14,16",  0,          "1,2,3,7,8,9,14,16" },
    { "2,3,4,5",        0,          "2,3,4,5" },
    { "",               0,          ""},
    { "1048576",        0,          "1048576"},

    { "[2]",            0,          "2" },
    { "[7-9]",          0,          "7,8,9" },
    { "[2,3,4,5]",      0,          "2,3,4,5" },
    { "[0]",            0,          "0" },
    { "[]",             0,          ""},

    { "2",              IDSET_FLAG_RANGE,  "2" },
    { "7-9",            IDSET_FLAG_RANGE,  "7-9" },
    { "1,7-9",          IDSET_FLAG_RANGE,  "1,7-9" },
    { "1,7-9,16",       IDSET_FLAG_RANGE,  "1,7-9,16" },
    { "1,7-9,14,16",    IDSET_FLAG_RANGE,  "1,7-9,14,16" },
    { "1-3,7-9,14,16",  IDSET_FLAG_RANGE,  "1-3,7-9,14,16" },
    { "2,3,4,5",        IDSET_FLAG_RANGE,  "2-5" },
    { "",               IDSET_FLAG_RANGE,  ""},

    { "2",             IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "2" },
    { "7-9",           IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[7-9]" },
    { "1,7-9",         IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9]" },
    { "1,7-9,16",      IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9,16]" },
    { "1,7-9,14,16",   IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9,14,16]" },
    { "1-3,7-9,14,16", IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1-3,7-9,14,16]"},
    { "2,3,4,5",       IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[2-5]" },
    { "",              IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, ""},

    /* expected failures */
    { "4.2",            0,          NULL },
    { "x",              0,          NULL },
    { "1-2x",           0,          NULL },
    { "01,2",           0,          NULL },
    { "00",             0,          NULL },
    { "3,2",            0,          NULL },
    { "3-0",            0,          NULL },
    { "2,2,2,2",        0,          NULL },
    { "[0",             0,          NULL },
    { "0]",             0,          NULL },
    { "[[0]]",          0,          NULL },
    { "[[0,2]",         0,          NULL },
    { "[0,2]]",         0,          NULL },
    { "0,[2",           0,          NULL },
    { "0]2",            0,          NULL },
    { "0-",             0,          NULL },
    { "[0-]",           0,          NULL },
    { "-5",             0,          NULL },
    { "[-5]",           0,          NULL },

    { NULL, 0, NULL },
};

void test_basic (void)
{
    struct idset *idset;

    idset = idset_create (0, 0);
    ok (idset != NULL,
        "idset_create size=0 works");
    ok (idset_count (idset) == 0,
        "idset_count returns 0");
    ok (idset_empty (idset) == true,
        "idset_empty returns true");

    idset_destroy (idset);
}

void test_codec (void)
{
    struct inout *ip;

    for (ip = &test_inputs[0]; ip->in != NULL; ip++) {
        struct idset *idset;

        errno = 0;
        idset = idset_decode (ip->in);
        if (ip->out == NULL) { // expected fail
            ok (idset == NULL && errno == EINVAL,
                "idset_encode flags=0x%x '%s' fails with EINVAL",
                    ip->flags, ip->in);
        }
        else {
            ok (idset != NULL,
                "idset_decode '%s' works", ip->in);
            if (idset != NULL) {
                char *s = idset_encode (idset, ip->flags);
                bool match = (s && streq (s, ip->out));
                ok (match == true,
                    "idset_encode flags=0x%x '%s'->'%s' works",
                    ip->flags, ip->in, ip->out);
                if (!match)
                    diag ("%s", s ? s : "NULL");
                free (s);
            }
        }
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
        "idset_set(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_set (idset, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "idset_set(id=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_set (idset, 101) < 0 && errno == EINVAL,
        "idset_set(id=out of range) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (NULL, 1, 2) < 0 && errno == EINVAL,
        "idset_range_set(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, 1, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "idset_range_set(hi=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, IDSET_INVALID_ID, 1) < 0 && errno == EINVAL,
        "idset_range_set(lo=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, 101, 1) < 0 && errno == EINVAL,
        "idset_range_set(lo=out of range) fails with EINVAL");
    errno = 0;
    ok (idset_range_set (idset, 1, 101) < 0 && errno == EINVAL,
        "idset_range_set(hi=out of range) fails with EINVAL");

    errno = 0;
    ok (idset_clear (NULL, 1) < 0 && errno == EINVAL,
        "idset_clear(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_clear (idset, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "idset_clear(id=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_clear (idset, 101) == 0,
        "idset_clear(id=out of range) works");
    errno = 0;
    ok (idset_range_clear (NULL, 1, 2) < 0 && errno == EINVAL,
        "idset_range_clear(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_range_clear (idset, 1, IDSET_INVALID_ID) < 0 && errno == EINVAL,
        "idset_range_clear(hi=INVALID) fails with EINVAL");
    errno = 0;
    ok (idset_range_clear (idset, IDSET_INVALID_ID, 1) < 0 && errno == EINVAL,
        "idset_range_clear(lo=INVALID) fails with EINVAL");

    ok (idset_test (NULL, 1) == false,
        "idset_test(idset=NULL) returns false");

    ok (idset_count (NULL) == 0,
        "idset_count(idset=NULL) returns 0");

    ok (idset_universe_size (NULL) == 0,
        "idset_universe_size(idset=NULL) returns 0");

    ok (idset_empty (NULL) == true,
        "idset_empty(idset=NULL) returns true");

    errno = 0;
    ok (idset_copy (NULL) == NULL && errno == EINVAL,
        "idset_copy(idset=NULL) fails with EINVAL");

    ok (idset_first (NULL) == IDSET_INVALID_ID,
        "idset_first (idset=NULL) returns IDSET_INVALID_ID");
    ok (idset_next (NULL, 0) == IDSET_INVALID_ID,
        "idset_next (idset=NULL) returns IDSET_INVALID_ID");
    ok (idset_next (idset, IDSET_INVALID_ID) == IDSET_INVALID_ID,
        "idset_next (prev=INVALID) returns IDSET_INVALID_ID");
    ok (idset_next (idset, 101) == IDSET_INVALID_ID,
        "idset_next (prev=out of range) returns IDSET_INVALID_ID");
    ok (idset_prev (NULL, 0) == IDSET_INVALID_ID,
        "idset_prev (idset=NULL) returns IDSET_INVALID_ID");
    ok (idset_prev (idset, IDSET_INVALID_ID) == IDSET_INVALID_ID,
        "idset_prev (id=INVALID) returns IDSET_INVALID_ID");
    ok (idset_prev (idset, 101) == IDSET_INVALID_ID,
        "idset_next (id=out of range) returns IDSET_INVALID_ID");
    ok (idset_last (NULL) == IDSET_INVALID_ID,
        "idset_last (idset=NULL) returns IDSET_INVALID_ID");

    idset_destroy (idset);
}

void test_iter (void)
{
    struct idset *idset;
    struct idset *idset_nil;

    if (!(idset = idset_decode ("7-9")))
        BAIL_OUT ("idset_decode 7-9 failed");
    if (!(idset_nil = idset_create (0, 0)))
        BAIL_OUT ("idset_create (0, 0) failed");
    ok (idset_empty (idset) == false,
        "idset_empty (idset=[7-9]) returns false");
    ok (idset_empty (idset_nil) == true,
        "idset_empty (idset=[]) returns true");

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
    ok (idset_prev (idset, 9) == 8,
        "idset_prev idset=[7-9] id=9 returned 8");
    ok (idset_prev (idset, 8) == 7,
        "idset_prev idset=[7-9] id=8 returned 7");
    ok (idset_prev (idset, 7) == IDSET_INVALID_ID,
        "idset_prev idset=[7-9] id=7 returned INVALID");
    ok (idset_prev (idset, IDSET_INVALID_ID) == IDSET_INVALID_ID,
        "idset_prev idset=[7-9] id=INVALID returned INVALID");

    ok (idset_first (idset_nil) == IDSET_INVALID_ID,
        "idset_first idset=[] returned IDSET_INVALID_ID");
    ok (idset_last (idset_nil) == IDSET_INVALID_ID,
        "idset_last idset=[] returned IDSET_INVALID_ID");
    ok (idset_next (idset_nil, 0) == IDSET_INVALID_ID,
        "idset_next idset=[] prev=0 returned IDSET_INVALID_ID");

    idset_destroy (idset);
    idset_destroy (idset_nil);
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

typedef enum { OP_UNION, OP_DIFF, OP_INTER, OP_ADD, OP_SUB} op_t;
struct testop {
    const char *a;
    op_t op;
    const char *b;
    const char *result; // a if OP_ADD or OP_SUB, else return value
    int xrc;            // ignored unless OP_ADD or OP_SUB
    int errnum;
};

static struct testop optab[] = {
    { NULL,     OP_UNION,   "[0]",      NULL,       0,  EINVAL },
    { "[0]",    OP_UNION,   NULL,       "[0]",      0,  0 },
    { "[0]",    OP_UNION,   "[0]",      "[0]",      0,  0 },
    { "[0]",    OP_UNION,   "[1]",      "[0-1]",    0,  0 },
    { NULL,     OP_DIFF,    "[0]",      NULL,       0,  EINVAL },
    { "[0]",    OP_DIFF,    NULL,       "[0]",      0,  0 },
    { "[0]",    OP_DIFF,    "[0]",      "[]",       0,  0 },
    { "[0-1]",  OP_DIFF,    "[0]",      "[1]",      0,  0 },
    { NULL,     OP_INTER,   "[0]",      NULL,       0,  EINVAL },
    { "[0]",    OP_INTER,   NULL,       NULL,       0,  EINVAL },
    { "[0-1]",  OP_INTER,   "[2-3]",    "[]",       0,  0 },
    { "[0-1]",  OP_INTER,   "[1-2]",    "[1]",      0,  0 },
    { "[0-1]",  OP_INTER,   "[0-1]",    "[0-1]",    0,  0 },
    { NULL,     OP_ADD,     "[0]",      NULL,       -1, EINVAL },
    { "[0]",    OP_ADD,     NULL,       "[0]",      0,  0 },
    { "[0]",    OP_ADD,     "[0]",      "[0]",      0,  0 },
    { "[0]",    OP_ADD,     "[1]",      "[0,1]",    0,  0 },
    { NULL,     OP_SUB,     "[0]",      NULL,       -1, EINVAL },
    { "[0]",    OP_SUB,     NULL,       "[0]",      0,  0 },
    { "[0,1]",  OP_SUB,     "[1]",      "[0]",      0,  0 },
    { "[0,1]",  OP_SUB,     "[2]",      "[0,1]",    0,  0 },
};

static void tryop (const char *s1,
                   op_t op,
                   const char *s2,
                   const char *s3,
                   int xrc,
                   int errnum)
{
    struct idset *a = NULL;
    struct idset *b = NULL;
    struct idset *expect = NULL;
    struct idset *result = NULL;
    int rc = -1;

    if (s1) {
        if (!(a = idset_decode (s1)))
            BAIL_OUT ("tryop failed to decode %s", s1);
    }
    if (s2) {
        if (!(b = idset_decode (s2)))
            BAIL_OUT ("tryop failed to decode %s", s2);
    }
    if (s3) {
        if (!(expect = idset_decode (s3)))
            BAIL_OUT ("tryop failed to decode %s", s3);
    }
    errno = 0;
    switch (op) {
        case OP_UNION:
            result = idset_union (a, b);
            break;
        case OP_DIFF:
            result = idset_difference (a, b);
            break;
        case OP_INTER:
            result = idset_intersect (a, b);
            break;
        case OP_ADD:
            rc = idset_add (a, b);
            break;
        case OP_SUB:
            rc = idset_subtract (a, b);
            break;
    }
    /* a in add and subtract is in/out arg, and returns -1 or 0.
     */
    if (op == OP_ADD || op == OP_SUB) {
        ok (((xrc < 0 && xrc == rc && errno == errnum)
            || idset_equal (expect, a)),
            "idset_%s %s %s leaves arg1=%s%s%s",
            op == OP_ADD ? "add" : "subtract",
            s1 ? s1 : "NULL",
            s2 ? s2 : "NULL",
            s3 ? s3 : "NULL",
            xrc < 0 ? ", fails with " : "",
            xrc < 0 ? (errnum == EINVAL ? "EINVAL" : "expected errno") : "");
    }
    /* Other funcs return a new idset.
     */
    else {
        ok (((expect == result && errno == errnum)
            || idset_equal (expect, result)),
            "idset_%s %s %s %s%s%s",
            op == OP_UNION ? "union" : op == OP_DIFF
                           ? "difference" : "intersect",
            s1 ? s1 : "NULL",
            s2 ? s2 : "NULL",
            s3 ? "= " : "",
            s3 ? s3 : "fails with ",
            errnum > 0 ? (errnum == EINVAL ? "EINVAL" : "expected errno") : "");
    }

    idset_destroy (a);
    idset_destroy (b);
    idset_destroy (expect);
    idset_destroy (result);
}

void test_ops (void)
{
    for (int i = 0; i < ARRAY_SIZE (optab); i++) {
        tryop (optab[i].a,
               optab[i].op,
               optab[i].b,
               optab[i].result,
               optab[i].xrc,
               optab[i].errnum);
    }
    struct idset *a;

    if (!(a = idset_decode ("1-10")))
        BAIL_OUT ("idset_decode [1-10] failed");
    idset_clear_all (a);
    ok (idset_count (a) == 0,
        "idset_clear_all results in empty set");
    idset_destroy (a);
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
    ok (idset_universe_size (idset) == 1,
        "idset_universe_size is 1");
    ok (idset_set (idset, 0) == 0,
        "idset_set 0 works");
    errno = 0;
    ok (idset_set (idset, 1) < 0 && errno == EINVAL,
        "idset_set 1 fails with EINVAL");
    ok (idset_clear (idset, 1) == 0,
        "idset_clear 1 is a no-op");
    idset_destroy (idset);

    idset = idset_create (1, IDSET_FLAG_AUTOGROW);
    ok (idset != NULL,
        "idset_create size=1 flags=AUTOGROW works");
    ok (idset_universe_size (idset) == 1,
        "idset_universe_size is 1");
    ok (idset_set (idset, 0) == 0,
        "idset_set 0 works");
    ok (idset_clear (idset, 2) == 0
        && idset_universe_size (idset) == 1,
        "idset_clear 2 is a no-op");
    ok (idset_set (idset, 2) == 0,
        "idset_set 2 works");
    ok (idset_universe_size (idset) > 1,
        "idset_universe_size returned a larger size");
    ok (   idset_test (idset, 0)
        && !idset_test (idset, 1)
        && idset_test (idset, 2)
        && !idset_test (idset, 3),
        "idset contains expected ids");
    idset_destroy (idset);
}

/* N.B. internal function */
void test_format_first (void)
{
    char buf[64];

    ok (format_first (buf, sizeof (buf), "[]xyz", 42) == 0
        && streq (buf, "42xyz"),
        "format_first works with leading idset");
    ok (format_first (buf, sizeof (buf), "abc[]xyz", 42) == 0
        && streq (buf, "abc42xyz"),
        "format_first works with mid idset");
    ok (format_first (buf, sizeof (buf), "abc[]", 42) == 0
        && streq (buf, "abc42"),
        "format_first works with end idset");

    errno = 0;
    ok (format_first (buf, sizeof (buf), "abc", 42) < 0
        && errno == EINVAL,
        "format_first fails with EINVAL no brackets");

    errno = 0;
    ok (format_first (buf, sizeof (buf), "abc[", 42) < 0
        && errno == EINVAL,
        "format_first fails with EINVAL with no close bracket");

    errno = 0;
    ok (format_first (buf, sizeof (buf), "abc]", 42) < 0
        && errno == EINVAL,
        "format_first fails with EINVAL with no open bracket");

    errno = 0;
    ok (format_first (buf, sizeof (buf), "abc][", 42) < 0
        && errno == EINVAL,
        "format_first fails with EINVAL with backwards brackets");

    errno = 0;
    ok (format_first (buf, 4, "abc[]", 1) < 0
        && errno == EOVERFLOW,
        "format_first fails with EOVERFLOW when buffer exhausted");
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

/* At size 32, veb_pred() returns T.M when checking T.M-1.
 * We added a workaround, and a TODO test in libutil/test/veb.c for now.
 * This checks size 31, 32, 33.
 */
void issue_2336 (void)
{
    struct idset *idset;
    unsigned int t, u, M;
    int failure = 0;

    for (M = 31; M <= 33; M++) {
        if (!(idset = idset_create (M, 0)))
            BAIL_OUT ("idset_create size=32 failed");
        for (t = 0; t < M; t++) {
            if (idset_set (idset, t) < 0)
                BAIL_OUT ("idset_set %u failed", t);
            u = idset_last (idset);
            if (u != t) {
                diag ("idset_last %u returned %u", t, u);
                failure++;
            }
        }
        ok (failure == 0,
            "2336: idset_last works for all bits in size=%u idset", M);
        idset_destroy (idset);
    }
}

void test_initfull (void)
{
    struct idset *idset;
    size_t size = 128;

    idset = idset_create (size, IDSET_FLAG_INITFULL);
    ok (idset != NULL,
        "idset_create size=%zu flags=INITFULL works", size);
    ok (idset_count (idset) == size,
        "idset_count returns correct size");
    ok (idset_test (idset, 0) == true,
        "idset_test 0 is true");
    ok (idset_test (idset, size - 1) == true,
        "idset_test %zu is true", size - 1);
    ok (idset_first (idset) == 0,
        "idset_first returns 0");
    ok (idset_next (idset, 0) == 1,
        "idset_next prev=0 returns 1");
    ok (idset_test (idset, size) == false,
        "idset_test %zu is false", size);
    idset_destroy (idset);

    // grow by clearing a bit
    idset = idset_create (0, IDSET_FLAG_INITFULL | IDSET_FLAG_AUTOGROW);
    ok (idset != NULL,
        "idset_create size=0 flags=INITFULL|AUTOGROW works");
    ok (idset_clear (idset, IDSET_DEFAULT_SIZE) == 0,
        "idset_clear id=size works");
    ok (idset_count (idset) == IDSET_DEFAULT_SIZE*2 - 1,
        "idset_count returns 2*default size - 1");
    ok (idset_test (idset, IDSET_DEFAULT_SIZE) == false,
        "idset_test id=size is false");
    ok (idset_test (idset, IDSET_DEFAULT_SIZE + 1) == true,
        "idset_test id=size+1 is true");
    ok (idset_test (idset, IDSET_DEFAULT_SIZE*2 - 1) == true,
        "idset_test id=2*size-1 is true");
    idset_destroy (idset);

    // setting a bit should not cause growth
    idset = idset_create (4, IDSET_FLAG_INITFULL | IDSET_FLAG_AUTOGROW);
    ok (idset != NULL,
        "idset_create size=4 flags=INITFULL|AUTOGROW works");
    ok (idset_count (idset) == 4,
        "idset_count returns 4");
    ok (idset_set (idset, 4) == 0,
        "idset_set 4 (out of range) works");
    ok (idset_count (idset) == 4,
        "idset_count still returns 4");
    idset_destroy (idset);
}

void test_alloc (int flags)
{
    struct idset *idset;
    unsigned int ids[64];
    unsigned int id;
    int errors;
    size_t size_before;
    size_t size_after;

    flags |= IDSET_FLAG_AUTOGROW | IDSET_FLAG_INITFULL;
    idset = idset_create (16, flags);
    if (!idset)
        BAIL_OUT ("could not create idset");

    ok (idset_count (idset) == 16,
        "idset_count returns 16");

    size_before = idset_universe_size (idset);
    errors = 0;
    for (int i = 0; i < ARRAY_SIZE (ids); i++) {
        if (idset_alloc (idset, &ids[i]) < 0)
            errors++;
    }
    size_after = idset_universe_size (idset);
    ok (errors == 0,
        "idset_alloc allocated multiple ids with no errors");
    errors = 0;
    for (int i = 0; i < ARRAY_SIZE (ids); i++)
        if (ids[i] != i) {
            diag ("allocation %d is %lu", i, ids[i]);
            errors++;
        }
    ok (errors == 0,
        "ids were allocated monotonically");
    ok (size_before < size_after,
        "idset size grew automatically");
    diag ("before=%zu after=%zu", size_before, size_after);
    ok (idset_count (idset) == 0,
        "idset_count returns 0");

    errors = 0;
    for (int i = 0; i < ARRAY_SIZE (ids); i += 2) {
        if (idset_free_check (idset, ids[i]) < 0) {
            diag ("idset_free_check %lu: %s", ids[i], strerror (errno));
            errors++;
        }
    }
    ok (errors == 0,
        "idset_free_check freed multiple ids with no errors");
    ok (idset_count (idset) == 32,
        "idset_count returns 32");

    errors = 0;
    for (int i = 0; i < ARRAY_SIZE (ids); i += 2) {
        if (idset_alloc (idset, &ids[i]) < 0)
            errors++;
    }
    ok (errors == 0,
        "idset_alloc re-allocated multiple ids with no errors");

    errors = 0;
    for (int i = 0; i < ARRAY_SIZE (ids); i++)
        if (ids[i] != i) {
            diag ("allocation %d is %lu", i, ids[i]);
            errors++;
        }
    ok (errors == 0,
        "ids were allocated monotonically");
    ok (idset_count (idset) == 0,
        "idset_count returns 0");

    for (int i = 0; i < ARRAY_SIZE (ids); i++)
        idset_free (idset, ids[i]);
    ok (idset_count (idset) == idset_universe_size (idset),
        "idset_free freed all ids");

    idset_destroy (idset);
    idset = idset_create (16, IDSET_FLAG_INITFULL);
    if (!idset)
        BAIL_OUT ("could not create idset");
    for (int i = 0; i < 16; i++)
        if (idset_alloc (idset, &id) < 0)
            BAIL_OUT ("could not allocate ids in existing universe");
    errno = 0;
    ok (idset_alloc (idset, &id) < 0 && errno == EINVAL,
        "idset_alloc fails with EINVAL when universe is full and no autofree");
    idset_destroy (idset);
}

void test_alloc_rr (void)
{
    unsigned int id;
    struct idset *idset;

    idset = idset_create (2, IDSET_FLAG_INITFULL | IDSET_FLAG_ALLOC_RR);
    if (!idset)
        BAIL_OUT ("could not create idset");
    diag ("alloc_rr: created set size=2");
    ok (idset_alloc (idset, &id) == 0 && id == 0,
        "alloc_rr: allocated 0");
    idset_free (idset, 0);
    diag ("alloc_rr: freed 0");
    ok (idset_alloc (idset, &id) == 0 && id == 1,
        "alloc_rr: allocated 1");
    idset_free (idset, 1);
    diag ("alloc_rr: freed 1");
    ok (idset_alloc (idset, &id) == 0 && id == 0,
        "alloc_rr: allocated 0");
    ok (idset_alloc (idset, &id) == 0 && id == 1,
        "alloc_rr: allocated 1");
    ok (idset_alloc (idset, &id) < 0,
        "alloc_rr: failed");
    idset_free (idset, 0);
    diag ("alloc_rr: freed 0");
    ok (idset_alloc (idset, &id) == 0 && id == 0,
        "alloc_rr: allocated 0");

    idset_destroy (idset);
}

void test_alloc_badparam (void)
{
    unsigned int id;
    struct idset *idset;
    struct idset *idset2;

    idset = idset_create (16, IDSET_FLAG_INITFULL | IDSET_FLAG_AUTOGROW);
    if (!idset)
        BAIL_OUT ("could not create idset");
    idset2 = idset_create (16, 0);
    if (!idset2)
        BAIL_OUT ("could not create idset");

    errno = 0;
    ok (idset_alloc (NULL, &id) < 0 && errno == EINVAL,
        "idset_alloc idset=NULL fails with EINVAL");
    errno = 0;
    ok (idset_alloc (idset2, &id) < 0 && errno == EINVAL,
        "idset_alloc fails with EINVAL without IDSET_FLAG_INITFULL");
    errno = 0;
    ok (idset_alloc (idset, NULL) < 0 && errno == EINVAL,
        "idset_alloc id=NULL fails with EINVAL");

    lives_ok ({idset_free (NULL, 42);},
        "idset_free idset=NULL doesn't crash");
    idset_free (idset2, 2);
    diag ("idset_free without IDSET_FLAG_INITFULL is a no-op");

    errno = 0;
    ok (idset_free_check (NULL, 2) < 0 && errno == EINVAL,
        "idset_free_check idset=NULL fails with EINVAL");
    errno = 0;
    ok (idset_free_check (idset2, 2) < 0 && errno == EINVAL,
        "idset_free_check without IDSET_FLAG_INITFULL fails with EINVAL");
    errno = 0;
    ok (idset_free_check (idset, 16) < 0 && errno == EINVAL,
        "idset_free_check without out of range fails with EINVAL");
    errno = 0;
    ok (idset_free_check (idset, 2) < 0 && errno == EEXIST,
        "idset_free_check on non-free id fails with EEXIST");

    idset_destroy (idset);
    idset_destroy (idset2);
}

void test_decode_ex (void)
{
    struct idset *idset;
    idset_error_t error;

    /* First, for fun, just generate some run of the mill parsing errors that
     * are already tested above and show the textual errors on the diag output.
     */
    errno = 0;
    error.text[0] = '\0';
    idset = idset_decode_ex ("0", 1, 0, 0xffff, &error);
    ok (idset == NULL && errno == EINVAL && strlen (error.text) > 0,
        "idset_decode_ex flags=0xffff fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    idset = idset_decode_ex ("[0-1", 4, 0, 0, &error);
    ok (idset == NULL && errno == EINVAL && strlen (error.text) > 0,
        "idset_decode_ex s=[0-1 fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    idset = idset_decode_ex ("2,1,0", 4, 0, 0, &error);
    ok (idset == NULL && errno == EINVAL && strlen (error.text) > 0,
        "idset_decode_ex s=2,1,0 fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    idset = idset_decode_ex ("0-255xxx", 8, 0, 0, &error);
    ok (idset == NULL && errno == EINVAL && strlen (error.text) > 0,
        "idset_decode_ex s=0-255xxx fails with EINVAL");
    diag ("%s", error.text);

    /* Decode to a fixed size (256) set
     * This works because len=5 turns the input into 0-255
     */
    idset = idset_decode_ex ("0-255,256", 5, 256, 0, &error);
    ok (idset != NULL,
        "idset_decode_ex s=0-255,256 len=5 size=256 works");
    idset_destroy (idset);

    /* Overflow a fixed size (256) set
     */
    errno = 0;
    error.text[0] = '\0';
    idset = idset_decode_ex ("0-255,256", 9, 256, 0, &error);
    ok (idset == NULL && errno == EINVAL && strlen (error.text) > 0,
        "idset_decode_ex s=0-255,256 len=9 size=256 fails with EINVAL");
    diag ("%s", error.text);

    /* Show that overflow is handled with AUTOGROW.
     */
    idset = idset_decode_ex ("255,256", 7, 256, IDSET_FLAG_AUTOGROW, &error);
    ok (idset != NULL,
        "idset_decode_ex s=255,256 size=256 works with AUTOGROW");
    idset_destroy (idset);

    /* An empty set with size=-1 is not allowed without AUTOGROW.
     */
    errno = 0;
    error.text[0] = '\0';
    idset = idset_decode_ex ("", -1, -1, 0, &error);
    ok (idset == NULL && errno == EINVAL && strlen (error.text) > 0,
        "idset_decode_ex s=\"\" size=-1 fails with EINVAL");
    diag ("%s", error.text);

    /* But an empty set with size=-1 and AUTOGROW is allowed.
     */
    idset = idset_decode_ex ("", -1, -1, IDSET_FLAG_AUTOGROW, &error);
    ok (idset != NULL,
        "idset_decode_ex s=\"\" size=-1 works with AUTOGROW");
    idset_destroy (idset);

    /* A set with size-1 exactly fits the max id
     */
    idset = idset_decode_ex ("1,3,5", -1, -1, 0, &error);
    ok (idset != NULL,
        "idset_decode_ex s=1,3,5 size=-1 works");
    ok (idset_universe_size (idset) == 6,
        "idset_universe_size returns 6");
    idset_destroy (idset);
}

void test_decode_empty (void)
{
    ok (idset_decode_empty ("[]", -1) == true,
        "idset_decode_empty [] returns true");
    ok (idset_decode_empty ("", -1) == true,
        "idset_decode_empty \"\" returns true");
    ok (idset_decode_empty ("1-4", -1) == false,
        "idset_decode_empty 1-4 returns false");
    ok (idset_decode_empty ("[", -1) == false,
        "idset_decode_empty [ returns false");
    ok (idset_decode_empty (NULL, -1) == false,
        "idset_decode_empty NULL returns false");
}

struct infovec {
    const char *input;
    int errnum;
    size_t count;
    unsigned int maxid;
};

struct infovec infovec[] = {
    { "[]", 0, 0, IDSET_INVALID_ID },
    { "", 0, 0, IDSET_INVALID_ID },
    { "[", EINVAL, 0, 0 },
    { NULL, EINVAL, 0, 0 },
    { "0", 0, 1, 0 },
    { "1,2", 0, 2, 2 },
    { "1,2-1024", 0, 1024, 1024 },
    { "0-3", 0, 4, 3 },
};


void test_decode_info (void)
{
    for (int i = 0; i < ARRAY_SIZE (infovec); i++) {
        idset_error_t error;
        size_t count = 0;
        unsigned int maxid = 0;
        int rc;

        errno = 0;
        error.text[0] = '\0';

        rc = idset_decode_info (infovec[i].input,
                                -1,
                                &count,
                                &maxid,
                                &error);
        if (infovec[i].errnum != 0) {
            ok (rc < 0
                && errno == infovec[i].errnum
                && strlen (error.text) > 0,
                "idset_decode_info %s failed with expected error",
                infovec[i].input ? infovec[i].input : "NULL");
            diag ("%s", error.text);
        }
        else {
            ok (rc == 0
                && count == infovec[i].count
                && maxid == infovec[i].maxid,
                "idset_decode_info %s  works",
                infovec[i].input);
        }
    }

    ok (idset_decode_info ("1", -1, NULL, NULL, NULL) == 0,
        "idset_decode_info accepts NULL maxid/count");
}

void test_decode_addsub (void)
{
    struct idset *idset;
    idset_error_t error;

    if (!(idset = idset_create (0, IDSET_FLAG_AUTOGROW)))
        BAIL_OUT ("idset_create failed");

    ok (idset_decode_add (idset, "1-4", -1, &error) == 0
        && idset_count (idset) == 4,
        "idset_decode_add 1-4 works");
    ok (idset_decode_add (idset, "5-8", -1, &error) == 0
        && idset_count (idset) == 8,
        "idset_decode_add 5-8 works");
    ok (idset_decode_add (idset, "1,5", -1, &error) == 0
        && idset_count (idset) == 8,
        "idset_decode_add 1,5 works");
    ok (idset_decode_subtract (idset, "1,5", -1, &error) == 0
        && idset_count (idset) == 6,
        "idset_decode_subtract 1,5 works");
    ok (idset_decode_subtract (idset, "", -1, &error) == 0
        && idset_count (idset) == 6,
        "idset_decode_subtract \"\"  works");
    ok (idset_decode_subtract (idset, "0-100", -1, &error) == 0
        && idset_count (idset) == 0,
        "idset_decode_subtract 0-100 works");

    errno = 0;
    error.text[0] = '\0';
    ok  (idset_decode_add (idset, "[", -1, &error) < 0
         && errno == EINVAL
         && strlen (error.text) > 0,
         "idset_decode_add [ fails with errno and error");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok  (idset_decode_subtract (idset, "]", -1, &error) < 0
         && errno == EINVAL
         && strlen (error.text) > 0,
         "idset_decode_subtract ] fails with errno and error");
    diag ("%s", error.text);

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
    test_format_first ();
    issue_1974 ();
    issue_2336 ();
    test_ops ();
    test_initfull();
    diag ("idset_alloc test flags=0");
    test_alloc (0);
    diag ("idset_alloc test flags=COUNT_LAZY");
    test_alloc (IDSET_FLAG_COUNT_LAZY);
    diag ("idset_alloc test flags=ALLOC_RR");
    test_alloc (IDSET_FLAG_ALLOC_RR);
    test_alloc_rr ();
    test_alloc_badparam();
    test_decode_ex ();
    test_decode_empty ();
    test_decode_info ();
    test_decode_addsub ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
