/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include <stdlib.h>
#include "src/common/libtap/tap.h"
#include "src/common/libutil/veb.h"

void empty_pred_test1 (void)
{
    uint M = 1 << 16;
    Veb T = vebnew (M, 0);
    ok (T.D != NULL, "empty_pred_test1 vebnew OK");
    vebput (T, 0xf000);
    ok (vebpred (T, 0xf000) == 0xf000);
    vebput (T, 0x0f00);
    ok (vebpred (T, 0x0f00) == 0x0f00);
    vebput (T, 0x00f0);
    ok (vebpred (T, 0x00f0) == 0x00f0);
    vebput (T, 0x000f);
    ok (vebpred (T, 0x000f) == 0x000f);
    vebdel (T, 0xf000);
    ok (vebpred (T, 0xf000) != 0xf000);
    vebdel (T, 0x0f00);
    ok (vebpred (T, 0x0f00) != 0x0f00);
    vebdel (T, 0x00f0);
    ok (vebpred (T, 0x00f0) != 0x00f0);
    vebdel (T, 0x000f);
    ok (vebpred (T, 0x000f) != 0x000f);
    free (T.D);
}

void empty_pred_test2 (void)
{
    uint M = 1 << 16;
    Veb T = vebnew (M, 0);
    ok (T.D != NULL, "empty_pred_test2 vebnew OK");
    vebput (T, 0xf000);
    ok (vebpred (T, 0xf000) == 0xf000);
    vebput (T, 0x0f00);
    ok (vebpred (T, 0x0f00) == 0x0f00);
    vebput (T, 0x00f0);
    ok (vebpred (T, 0x00f0) == 0x00f0);
    vebput (T, 0x000f);
    ok (vebpred (T, 0x000f) == 0x000f);
    uint x = vebpred (T, M - 1);
    ok (x == 0xf000);
    x = vebpred (T, x - 1);
    ok (x == 0x0f00);
    x = vebpred (T, x - 1);
    ok (x == 0x00f0);
    x = vebpred (T, x - 1);
    ok (x == 0x000f);
    x = vebpred (T, x - 1);
    ok (x == M);
    free (T.D);
}

Veb empty_pred_load_test1_fill (uint M)
{
    Veb T = vebnew (M, 0);
    if (T.D) {
        for (int i = 0; i < 1000; ++i) {
            uint x = rand () % M;
            vebput (T, x);
        }
    }
    return T;
}

void empty_pred_load_test1 (void)
{
    int errors = 0;
    srand (433849);
    uint M = rand () % (1 << 16);
    Veb T = empty_pred_load_test1_fill (M);
    ok (T.D != NULL, "empty_pred_load_test1 vebnew OK");
    uint i = i = vebpred (T, M - 1);
    while (i < M) {
        vebdel (T, i);
        uint j = vebpred (T, i);
        if (i == j)
            errors++;
        i = j;
    }
    ok (errors == 0, "empty_pred_load_test1 no errors");
    free (T.D);
}

uint empty_pred_load_test2_fill (Veb T, uint m)
{
    uint n = 0;
    for (int i = 0; i < m; ++i) {
        uint x = rand () % T.M;
        if (vebpred (T, x) != x) {
            vebput (T, x);
            ++n;
        }
    }
    return n;
}

void empty_pred_load_test2 (void)
{
    srand (83843);
    uint M = rand () % (1 << 16);
    Veb T = vebnew (M, 0);
    ok (T.D != NULL, "empty_pred_load_test2 vebnew OK");
    uint m = empty_pred_load_test2_fill (T, 1000);
    uint n = 0;
    uint i = vebpred (T, M - 1);
    while (i != M) {
        ++n;
        i = vebpred (T, i - 1);
    }
    ok (n == m, "empty_pred_load_test2 correct count");
    free (T.D);
}

void empty_succ_test1 (void)
{
    uint M = 1 << 16;
    Veb T = vebnew (M, 0);
    ok (T.D != NULL, "empty_succ_test1 vebnew OK");
    vebput (T, 0x000f);
    ok (vebsucc (T, 0x000f) == 0x000f);
    vebput (T, 0x00f0);
    ok (vebsucc (T, 0x00f0) == 0x00f0);
    vebput (T, 0x0f00);
    ok (vebsucc (T, 0x0f00) == 0x0f00);
    vebput (T, 0xf000);
    ok (vebsucc (T, 0xf000) == 0xf000);
    vebdel (T, 0x000f);
    ok (vebsucc (T, 0x000f) != 0x000f);
    vebdel (T, 0x00f0);
    ok (vebsucc (T, 0x00f0) != 0x00f0);
    vebdel (T, 0x0f00);
    ok (vebsucc (T, 0x0f00) != 0x0f00);
    vebdel (T, 0xf000);
    ok (vebsucc (T, 0xf000) != 0xf000);
    free (T.D);
}

void empty_succ_test2 (void)
{
    uint M = 1 << 16;
    Veb T = vebnew (M, 0);
    ok (T.D != NULL, "empty_succ_test2 vebnew OK");
    vebput (T, 0x000f);
    ok (vebsucc (T, 0x000f) == 0x000f);
    vebput (T, 0x00f0);
    ok (vebsucc (T, 0x00f0) == 0x00f0);
    vebput (T, 0x0f00);
    ok (vebsucc (T, 0x0f00) == 0x0f00);
    vebput (T, 0xf000);
    ok (vebsucc (T, 0xf000) == 0xf000);
    uint x = vebsucc (T, 0);
    ok (x == 0x000f);
    x = vebsucc (T, x + 1);
    ok (x == 0x00f0);
    x = vebsucc (T, x + 1);
    ok (x == 0x0f00);
    x = vebsucc (T, x + 1);
    ok (x == 0xf000);
    x = vebsucc (T, x + 1);
    ok (x == M);
    free (T.D);
}

Veb empty_succ_load_test1_fill (uint M)
{
    int errors = 0;
    Veb T = vebnew (M, 0);
    ok (T.D != NULL, "empty_succ_load_test1 vebnew OK");
    for (int i = 0; i < 0xff; ++i) {
        uint x = rand () % M;
        vebput (T, x);
        if (vebsucc (T, x) != x)
            errors++;
    }
    ok (errors == 0, "empty_succ_load_test1 random fill OK");
    return T;
}

void empty_succ_load_test1 (void)
{
    int errors = 0;
    srand (438749);
    uint M = rand () % (1 << 16);
    Veb T = empty_succ_load_test1_fill (M);
    uint i = i = vebsucc (T, 0);
    while (i < M) {
        vebdel (T, i);
        uint j = vebsucc (T, i);
        if (i == j)
            errors++;
        i = j;
    }
    ok (errors == 0, "empty_succ_load_test1 no errors");
    free (T.D);
}

uint empty_succ_load_test2_fill (Veb T, uint m)
{
    uint n = 0;
    for (int i = 0; i < m; ++i) {
        uint x = rand () % T.M;
        if (vebsucc (T, x) != x) {
            vebput (T, x);
            ++n;
        }
    }
    return n;
}

void empty_succ_load_test2 (void)
{
    srand (83843);
    uint M = rand () % (1 << 16);
    Veb T = vebnew (M, 0);
    ok (T.D != NULL, "empty_succ_load_test2 vebnew OK");
    uint m = empty_succ_load_test2_fill (T, 1000);
    uint n = 0;
    uint i = vebsucc (T, 0);
    while (i != M) {
        ++n;
        i = vebsucc (T, i + 1);
    }
    ok (n == m, "empty_succ_load_test2 correct count");
    free (T.D);
}

void full_pred_test1 (void)
{
    uint M = 1 << 16;
    Veb T = vebnew (M, 1);
    ok (T.D != NULL, "full_pred_test1 vebnew OK");
    vebdel (T, 0xf000);
    ok (vebpred (T, 0xf000) != 0xf000);
    vebdel (T, 0x0f00);
    ok (vebpred (T, 0x0f00) != 0x0f00);
    vebdel (T, 0x00f0);
    ok (vebpred (T, 0x00f0) != 0x00f0);
    vebdel (T, 0x000f);
    ok (vebpred (T, 0x000f) != 0x000f);
    vebput (T, 0xf000);
    ok (vebpred (T, 0xf000) == 0xf000);
    vebput (T, 0x0f00);
    ok (vebpred (T, 0x0f00) == 0x0f00);
    vebput (T, 0x00f0);
    ok (vebpred (T, 0x00f0) == 0x00f0);
    vebput (T, 0x000f);
    ok (vebpred (T, 0x000f) == 0x000f);
    free (T.D);
}

Veb full_pred_load_test1_fill (uint M)
{
    int errors = 0;
    Veb T = vebnew (M, 1);
    ok (T.D != NULL, "full_pred_load_test1 vebnew OK");
    for (int i = 0; i < 0xff; ++i) {
        uint x = rand () % M;
        vebdel (T, x);
        if (vebpred (T, x) == x)
            errors++;
    }
    ok (errors == 0, "full_pred_load_test1 random fill OK");
    return T;
}

void full_pred_load_test1 (void)
{
    int errors = 0;
    srand (438749);
    uint M = rand () % (1 << 16);
    Veb T = full_pred_load_test1_fill (M);
    uint i = i = vebpred (T, M - 1);
    while (i < M) {
        vebdel (T, i);
        uint j = vebpred (T, i);
        if (i == j)
            errors++;
        i = j;
    }
    ok (errors == 0, "full_pred_load_test1 no errors");
    free (T.D);
}

uint full_pred_load_test2_reduce (Veb T, uint m)
{
    uint n = 0;
    for (int i = 0; i < m; ++i) {
        uint x = rand () % T.M;
        if (vebpred (T, x) == x) {
            vebdel (T, x);
            ++n;
        }
    }
    return n;
}

void full_pred_load_test2 (void)
{
    srand (83843);
    uint M = rand () % (1 << 16);
    Veb T = vebnew (M, 1);
    ok (T.D != NULL, "full_pred_load_test2 vebnew OK");
    uint m = full_pred_load_test2_reduce (T, 1000);
    uint n = 0;
    uint i = vebpred (T, M - 1);
    while (i != M) {
        ++n;
        i = vebpred (T, i - 1);
    }
    ok (n == M - m, "full_pred_load_test2 correct count");
    free (T.D);
}

void full_succ_test1 (void)
{
    uint M = 1 << 16;
    Veb T = vebnew (M, 1);
    ok (T.D != NULL, "full_succ_test1 vebnew OK");
    vebdel (T, 0x000f);
    ok (vebsucc (T, 0x000f) != 0x000f);
    vebdel (T, 0x00f0);
    ok (vebsucc (T, 0x00f0) != 0x00f0);
    vebdel (T, 0x0f00);
    ok (vebsucc (T, 0x0f00) != 0x0f00);
    vebdel (T, 0xf000);
    ok (vebsucc (T, 0xf000) != 0xf000);
    vebput (T, 0x000f);
    ok (vebsucc (T, 0x000f) == 0x000f);
    vebput (T, 0x00f0);
    ok (vebsucc (T, 0x00f0) == 0x00f0);
    vebput (T, 0x0f00);
    ok (vebsucc (T, 0x0f00) == 0x0f00);
    vebput (T, 0xf000);
    ok (vebsucc (T, 0xf000) == 0xf000);
    free (T.D);
}

Veb full_succ_load_test1_fill (uint M)
{
    int errors = 0;
    Veb T = vebnew (M, 1);
    ok (T.D != NULL, "full_succ_load_test1 vebnew OK");
    for (int i = 0; i < 0xff; ++i) {
        uint x = rand () % M;
        vebdel (T, x);
        if (vebsucc (T, x) == x)
            errors++;
    }
    ok (errors == 0, "full_succ_load_test1 random fill OK");
    return T;
}

void full_succ_load_test1 (void)
{
    int errors = 0;
    srand (438749);
    uint M = rand () % (1 << 16);
    Veb T = full_succ_load_test1_fill (M);
    uint i = i = vebsucc (T, 0);
    while (i < M) {
        vebdel (T, i);
        uint j = vebsucc (T, i);
        if (i == j)
            errors++;
        i = j;
    }
    ok (errors == 0, "full_succ_load_test1 no errors");
    free (T.D);
}

uint full_succ_load_test2_reduce (Veb T, uint m)
{
    uint n = 0;
    for (int i = 0; i < m; ++i) {
        uint x = rand () % T.M;
        if (vebsucc (T, x) == x) {
            vebdel (T, x);
            ++n;
        }
    }
    return n;
}

void full_succ_load_test2 (void)
{
    srand (83843);
    uint M = rand () % (1 << 16);
    Veb T = vebnew (M, 1);
    ok (T.D != NULL, "full_succ_load_test2 vebnew OK");
    uint m = full_succ_load_test2_reduce (T, 1000);
    uint n = 0;
    uint i = vebsucc (T, 0);
    while (i != M) {
        ++n;
        i = vebsucc (T, i + 1);
    }
    ok (n == M - m, "full_succ_load_test2 correct count");
    free (T.D);
}

void Tset (Veb veb, uint from, uint to, uint value)
{
    while (from < to) {
        if (value)
            vebput (veb, from);
        else
            vebdel (veb, from);
        from++;
    }
}

uint Tisset (Veb veb, uint from, uint to, uint value)
{
    while (from < to) {
        if (vebsucc (veb, from) == from) {
            if (!value)
                goto done;
        } else {
            if (value)
                goto done;
        }
        from++;
    }
done:
    return from;
}

void test_full_init (void)
{
    int i;
    uint pos;
    for (i = 0; i < 20; i++) {
        uint size = 1UL << i;
        Veb T = vebnew (size, 1);
        if (!T.D)
            BAIL_OUT ("out of memory");
        pos = Tisset (T, 0, size, 1);
        if (pos < size)
            diag ("bit %u for size %u not expected value", pos, size);
        ok (pos == size, "%s: %u all set", __FUNCTION__, size);
        free (T.D);
    }
}

void test_empty_init (void)
{
    int i;
    uint pos;
    for (i = 0; i < 20; i++) {
        uint size = 1UL << i;
        Veb T = vebnew (size, 0);
        if (!T.D)
            BAIL_OUT ("out of memory");
        pos = Tisset (T, 0, size, 0);
        if (pos < size)
            diag ("bit %u for size %u not expected value", pos, size);
        ok (pos == size, "%s: %u all clear", __FUNCTION__, size);
        free (T.D);
    }
}

int main (int argc, char** argv)
{
    plan (NO_PLAN);

    /* Plan9 style tests provided with libveb,
     * adapted for TAP.
     */

    empty_pred_test1 ();
    empty_pred_test2 ();
    empty_pred_load_test1 ();
    empty_pred_load_test2 ();

    empty_succ_test1 ();
    empty_succ_test2 ();
    empty_succ_load_test1 ();
    empty_succ_load_test2 ();

    full_pred_test1 ();
    full_pred_load_test1 ();
    full_pred_load_test2 ();

    full_succ_test1 ();
    full_succ_load_test1 ();
    full_succ_load_test2 ();

    /* Tests added for Flux.
     */
    test_empty_init ();
    test_full_init ();

    done_testing ();
}

/*
  vi:tabstop=4 shiftwidth=4 expandtab
 */
