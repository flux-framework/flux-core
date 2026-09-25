/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include "src/common/libtap/tap.h"
#include "src/common/libutil/instance_name.h"

static void check (const char *input, const char *expected)
{
    char buf[64];

    instance_name_sanitize (input, buf, sizeof (buf));
    is (buf, expected, "sanitize \"%s\" -> \"%s\"", input, expected);
}

int main (int argc, char *argv[])
{
    char buf[8];

    plan (NO_PLAN);

    check ("sys", "sys");
    check ("", "");
    check ("test-1234", "test-1234");
    check ("slurm.pmi.42.0", "slurm.pmi.42.0");

    /* Path separators become dashes, and a leading one is suppressed.
     */
    check ("/", "");
    check ("/a", "a");
    check ("/a/b", "a-b");
    check ("a/b", "a-b");

    /* The F58 prefix becomes the f58plain "f".
     */
    check ("ƒ81DcDV", "f81DcDV");
    check ("/ƒ81DcDV/ƒ5HLm9", "f81DcDV-f5HLm9");

    /* Anything else is dropped.
     */
    check ("a b", "ab");
    check ("a:b*c", "abc");
    check ("_a.b-c", "_a.b-c");

    /* Sanitizing an already sanitized name returns it unchanged.
     */
    check ("f81DcDV-f5HLm9", "f81DcDV-f5HLm9");

    /* A name that does not fit loses leading characters, since the tail
     * identifies this instance rather than its ancestors.
     */
    instance_name_sanitize ("sys-fAAAA-fBBBB", buf, sizeof (buf));
    is (buf, "A-fBBBB", "sanitize truncates from the front, keeping the tail");

    instance_name_sanitize ("/ƒAAAA/ƒBBBB", buf, sizeof (buf));
    is (buf, "A-fBBBB", "truncation counts sanitized, not input, characters");

    /* Degenerate buffer sizes do not overrun.
     */
    instance_name_sanitize ("abc", buf, 1);
    is (buf, "", "size=1 yields an empty string");
    instance_name_sanitize ("abc", buf, 2);
    is (buf, "c", "size=2 keeps the last character");

    /* A name is valid when sanitizing would not change it.
     */
    ok (instance_name_valid ("sys"), "sys is a valid instance name");
    ok (instance_name_valid ("test-1234"), "test-1234 is valid");
    ok (instance_name_valid ("a.b_c-d"), "a.b_c-d is valid");
    ok (instance_name_valid ("sys-f81DcDV-f5HLm9"), "a derived name is valid");
    ok (!instance_name_valid (NULL), "NULL is not valid");
    ok (!instance_name_valid (""), "the empty string is not valid");
    ok (!instance_name_valid ("foo/bar"), "a path separator is not valid");
    ok (!instance_name_valid ("foo bar"), "a space is not valid");
    ok (!instance_name_valid ("ƒoo"), "the F58 prefix is not valid");
    ok (instance_name_valid ("-foo"), "a leading dash is valid");

    char toolong[INSTANCE_NAME_MAX + 3];
    memset (toolong, 'a', sizeof (toolong) - 1);
    toolong[sizeof (toolong) - 1] = '\0';
    ok (!instance_name_valid (toolong), "an over-long name is not valid");
    toolong[INSTANCE_NAME_MAX] = '\0';
    ok (instance_name_valid (toolong), "a name of exactly the max is valid");

    done_testing ();
    return 0;
}

// vi:ts=4 sw=4 expandtab
