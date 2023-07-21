/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <string.h>
#include <stdbool.h>

#include "ccan/array_size/array_size.h"
#include "src/common/libtap/tap.h"
#include "parse.h"

struct tab_percent {
    const char *s;
    double val;
    bool ok;
};
const struct tab_percent ptab[] = {
    // bad
    { "10", 0, false },
    { "10%x", 0, false },
    { "-10%", 0, false },
    { "", 0, false },
    { "%", 0, false },
    { "x%", 0, false },
    { "110%", 0, false },
    // good
    { "0%", 0, true},
    { "10%", 0.1, true},
    { "50%", 0.5, true },
    { "100%", 1, true },
};

void test_percent (void)
{
    double d;
    int rc;

    lives_ok ({sdexec_parse_percent (NULL, &d);},
        "sdexec_parse_percent input=NULL doesn't crash");
    lives_ok ({sdexec_parse_percent ("x", NULL);},
        "sdexec_parse_percent value=NULL doesn't crash");

    for (int i = 0; i < ARRAY_SIZE (ptab); i++) {
        d = 0;
        rc = sdexec_parse_percent (ptab[i].s, &d);
        if (ptab[i].ok) {
            ok (rc == 0 && d == ptab[i].val,
                "sdexec_parse_percent val=%s works", ptab[i].s);
        }
        else {
            ok (rc == -1,
                "sdexec_parse_percent val=%s fails", ptab[i].s);
        }
    }
}

struct tab_bitmap {
    const char *s;
    uint8_t val[4];
    size_t val_size;
    bool ok;
};
const struct tab_bitmap btab[] = {
    // bad
    { "1-",             { 0, 0, 0, 0 },     0, false },
    { "x",              { 0, 0, 0, 0 },     0, false },
    // good
    { "",               { 0, 0, 0, 0 },     0, true },
    { "0",              { 1, 0, 0, 0 },     1, true },
    { "0-2,8",          { 7, 1, 0, 0 },     2, true },
    { "8-15,16-23",     { 0, 255, 255, 0 }, 3, true },
};

void test_bitmap (void)
{
    uint8_t *bitmap;
    size_t size;
    int rc;

    lives_ok ({sdexec_parse_bitmap (NULL, &bitmap, &size);},
        "sdexec_parse_bitmap input=NULL doesn't crash");
    lives_ok ({sdexec_parse_bitmap ("0", NULL, &size);},
        "sdexec_parse_bitmap bitmap=NULL doesn't crash");
    lives_ok ({sdexec_parse_bitmap ("0", &bitmap, NULL);},
        "sdexec_parse_bitmap size=NULL doesn't crash");

    for (int i = 0; i < ARRAY_SIZE (btab); i++) {
        bitmap = NULL;
        size = 0;
        rc = sdexec_parse_bitmap (btab[i].s, &bitmap, &size);
        if (btab[i].ok) {
            ok (rc == 0
                && size == btab[i].val_size
                && (size < 1 || bitmap[0] == btab[i].val[0])
                && (size < 2 || bitmap[1] == btab[i].val[1])
                && (size < 3 || bitmap[2] == btab[i].val[2])
                && (size < 4 || bitmap[3] == btab[i].val[3]),
                "sdexec_parse_bitmap val=%s works", btab[i].s);
        }
        else {
            ok (rc == -1,
                "sdexec_parse_bitmap val=%s fails", btab[i].s);
        }
        free (bitmap);
    }
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_percent ();
    test_bitmap ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
