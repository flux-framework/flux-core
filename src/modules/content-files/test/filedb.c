/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/modules/content-files/filedb.h"
#include "src/common/libutil/unlink_recursive.h"

void test_badargs (const char *dbpath)
{
    void *data;
    size_t size;
    const char *errstr;
    char longkey[8192];

    memset (longkey, 'x', sizeof (longkey));
    longkey[sizeof (longkey) - 1] = '\0';

    /* get */

    errno = 0;
    errstr = NULL;
    ok (filedb_get (dbpath, "/", &data, &size, &errstr) < 0 && errno == EINVAL,
        "filedb_get key=\"/\" failed with EINVAL");
    ok (errstr != NULL,
        "and error string was set");

    errno = 0;
    errstr = NULL;
    ok (filedb_get (dbpath, longkey, &data, &size, &errstr) < 0
        && errno == EOVERFLOW,
        "filedb_get key=<long> failed with EOVERFLOW");
    ok (errstr != NULL,
        "and error string was set");

    errno = 0;
    ok (filedb_get (dbpath, "noexist", &data, &size, &errstr) < 0
        && errno == ENOENT,
        "filedb_get key=\"\" failed with ENOENT");

    /* put */

    errno = 0;
    errstr = NULL;
    ok (filedb_put (dbpath, "", "", 1, &errstr) < 0 && errno == EINVAL,
        "filedb_put key=\"\" failed with EINVAL");
    ok (errstr != NULL,
        "and error string was set");

    errno = 0;
    errstr = NULL;
    ok (filedb_put (dbpath, longkey, "", 1, &errstr) < 0
        && errno == EOVERFLOW,
        "filedb_put key=<long> failed with EOVERFLOW");
    ok (errstr != NULL,
        "and error string was set");

    /* validate */

    errno = 0;
    errstr = NULL;
    ok (filedb_validate (dbpath, "..", &errstr) < 0 && errno == EINVAL,
        "filedb_validate key=\"..\" failed with EINVAL");
    ok (errstr != NULL,
        "and error string was set");

    errno = 0;
    errstr = NULL;
    ok (filedb_validate (dbpath, longkey, &errstr) < 0
        && errno == EOVERFLOW,
        "filedb_validate key=<long> failed with EOVERFLOW");
    ok (errstr != NULL,
        "and error string was set");
}

void test_simple (const char *dbpath)
{
    char val1[] = { 'a', 'b', 'c' };
    char val2[] = { 'z', 'y', 'x', 'w', 'v', 'u'};
    const char *errstr;
    void *data;
    size_t size;

    /* simple validate, put, get */

    ok (filedb_validate (dbpath, "key1", &errstr) < 0,
        "filedb_validate fails on non-existent key");
    ok (filedb_put (dbpath, "key1", val1, sizeof (val1), &errstr) == 0,
        "filedb_put key1={abc} works");
    ok (filedb_validate (dbpath, "key1", &errstr) == 0,
        "filedb_validate success existent key");
    size = 0;
    data = NULL;
    ok (filedb_get (dbpath, "key1", &data, &size, &errstr) == 0,
        "filedb_get key1 works");
    ok (data && size == sizeof (val1) && memcmp (data, val1, size) == 0,
        "and returned data matches");
    free (data);

    /* overwrite key is allowed (e.g. for checkpoint support) */

    ok (filedb_put (dbpath, "key1", val2, sizeof (val2), &errstr) == 0,
        "filedb_put key1={zyxwvu} works (overwrite)");
    ok (filedb_get (dbpath, "key1", &data, &size, &errstr) == 0,
        "filedb_get key1 works");
    ok (data && size == sizeof (val2) && memcmp (data, val2, size) == 0,
        "and returned the updated data");
    free (data);
}

int main (int argc, char *argv[])
{
    char dir[1024];
    const char *tmp = getenv ("TMPDIR");

    plan (NO_PLAN);

    if (!tmp)
        tmp = "/tmp";
    if (snprintf (dir, sizeof (dir), "%s/filedb.XXXXXX", tmp) >= sizeof (dir))
        BAIL_OUT ("internal buffer ovverflow");
    if (!mkdtemp (dir))
        BAIL_OUT ("mkdtemp failed");
    diag ("mkdir %s", dir);

    test_badargs (dir);
    test_simple (dir);

    if (unlink_recursive (dir) < 0)
        BAIL_OUT ("unlink_recursive failed");

    done_testing ();
    return (0);
}

// vi: ts=4 sw=4 expandtab
