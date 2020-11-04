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
#include <jansson.h>
#include <flux/core.h>
#include <stdlib.h>
#include <fcntl.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libidset/idset.h"

#include "src/modules/resource/rutil.h"

void test_match_request_sender (void)
{
    flux_msg_t *msg1, *msg2;

    if (!(msg1 = flux_request_encode ("fubar.baz", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    if (!(msg2 = flux_request_encode ("fubaz.bar", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    ok (rutil_match_request_sender (msg1, NULL) == false,
        "rutil_match_request_sender msg2=NULL = false");
    ok (rutil_match_request_sender (NULL, msg2) == false,
        "rutil_match_request_sender msg1=NULL = false");

    ok (rutil_match_request_sender (msg1, msg2) == false,
        "rutil_match_request_sender msg1=(no sender) = false");

    if (flux_msg_push_route (msg1, "foo") < 0)
        BAIL_OUT ("flux_msg_push_route failed");

    ok (rutil_match_request_sender (msg1, msg2) == false,
        "rutil_match_request_sender msg2=(no sender) = false");

    if (flux_msg_push_route (msg2, "bar") < 0)
        BAIL_OUT ("flux_msg_push_route failed");

    ok (rutil_match_request_sender (msg1, msg2) == false,
        "rutil_match_request_sender different senders = false");

    char *id;
    if (flux_msg_pop_route (msg2, &id) < 0)
        BAIL_OUT ("flux_msg_clear_route failed");
    free (id);
    if (flux_msg_push_route (msg2, "foo") < 0)
        BAIL_OUT ("flux_msg_push_route failed");

    ok (rutil_match_request_sender (msg1, msg2) == true,
        "rutil_match_request_sender same senders = true");

    flux_msg_decref (msg1);
    flux_msg_decref (msg2);
}

void test_idset_sub (void)
{
    struct idset *ids1;
    struct idset *ids2;

    if (!(ids1 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (!(ids2 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");

    errno = 0;
    ok (rutil_idset_sub (NULL, ids2) < 0 && errno == EINVAL,
        "rutil_idset_sub ids1=NULL fails with EINVAL");

    ok (rutil_idset_sub (ids1, NULL) == 0 && idset_count (ids1) == 0,
        "rutil_idset_sub ids2=NULL has no effect");

    if (idset_set (ids1, 2) < 0)
        BAIL_OUT ("idset_set failed");
    if (idset_set (ids2, 42) < 0)
        BAIL_OUT ("idset_set failed");

    ok (rutil_idset_sub (ids1, ids2) == 0 && idset_count (ids1) == 1,
        "rutil_idset_sub non-overlapping idsets has no effect");

    if (idset_set (ids1, 42) < 0)
        BAIL_OUT ("idset_set failed");
    if (idset_set (ids2, 2) < 0)
        BAIL_OUT ("idset_set failed");

    ok (rutil_idset_sub (ids1, ids2) == 0 && idset_count (ids1) == 0,
        "rutil_idset_sub with overlap works");

    idset_destroy (ids1);
    idset_destroy (ids2);
}

void test_idset_add (void)
{
    struct idset *ids1;
    struct idset *ids2;

    if (!(ids1 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (!(ids2 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");

    errno = 0;
    ok (rutil_idset_add (NULL, ids2) < 0 && errno == EINVAL,
        "rutil_idset_add ids1=NULL fails with EINVAL");

    ok (rutil_idset_add (ids1, NULL) == 0 && idset_count (ids1) == 0,
        "rutil_idset_add ids2=NULL has no effect");

    if (idset_set (ids1, 2) < 0)
        BAIL_OUT ("idset_set failed");
    if (idset_set (ids2, 42) < 0)
        BAIL_OUT ("idset_set failed");

    ok (rutil_idset_add (ids1, ids2) == 0 && idset_count (ids1) == 2,
        "rutil_idset_add of non-overlapping idset works");
    ok (rutil_idset_add (ids1, ids2) == 0 && idset_count (ids1) == 2,
        "rutil_idset_add of overlapping idset has no effect");

    idset_destroy (ids1);
    idset_destroy (ids2);
}

void test_idset_diff (void)
{
    struct idset *ids1;
    struct idset *ids2;
    struct idset *add;
    struct idset *sub;

    if (!(ids1 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (!(ids2 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");

    ok (rutil_idset_diff (NULL, ids2, &add, &sub) == 0
        && add == NULL
        && sub == NULL,
        "rutil_idset_diff ids1=NULL works");
    idset_destroy (add);
    idset_destroy (sub);

    ok (rutil_idset_diff (ids1, NULL, &add, &sub) == 0
        && add == NULL
        && sub == NULL,
        "rutil_idset_diff ids2=NULL works");
    idset_destroy (add);
    idset_destroy (sub);

    errno = 0;
    ok (rutil_idset_diff (ids1, ids2, NULL, &sub) < 0 && errno == EINVAL,
        "rutil_idset_diff add=NULL fails with EINVAL");
    errno = 0;
    ok (rutil_idset_diff (ids1, ids2, &add, NULL) < 0 && errno == EINVAL,
        "rutil_idset_diff sub=NULL fails with EINVAL");

    if (idset_set (ids1, 1) < 0 || idset_set (ids2, 2) < 0)
        BAIL_OUT ("idset_set failed");
    add = sub = NULL;
    ok (rutil_idset_diff (ids1, ids2, &add, &sub) == 0
        && add != NULL && idset_count (add) == 1 && idset_test (add, 2)
        && sub != NULL && idset_count (sub) == 1 && idset_test (sub, 1),
        "rutil_idset_diff [1] [2] sets add=[2] sub=[1]");
    idset_destroy (add);
    idset_destroy (sub);

    add = sub = NULL;
    ok (rutil_idset_diff (ids2, ids1, &add, &sub) == 0
        && add != NULL && idset_count (add) == 1 && idset_test (add, 1)
        && sub != NULL && idset_count (sub) == 1 && idset_test (sub, 2),
        "rutil_idset_diff [2] [1] sets add=[1] sub=[2]");
    idset_destroy (add);
    idset_destroy (sub);

    if (idset_set (ids1, 2) < 0)
        BAIL_OUT ("idset_set failed");
    add = sub = NULL;
    ok (rutil_idset_diff (ids1, ids2, &add, &sub) == 0
        && add == NULL
        && sub != NULL && idset_count (sub) == 1 && idset_test (sub, 1),
        "rutil_idset_diff [1-2] [2] sets add=NULL sub=[1]");
    idset_destroy (add);
    idset_destroy (sub);

    add = sub = NULL;
    ok (rutil_idset_diff (ids2, ids1, &add, &sub) == 0
        && add != NULL && idset_count (add) == 1 && idset_test (add, 1)
        && sub == NULL,
        "rutil_idset_diff [2] [1-2] sets add=[1] sub=NULL");
    idset_destroy (add);
    idset_destroy (sub);

    if (idset_set (ids2, 1) < 0)
        BAIL_OUT ("idset_set failed");
    add = sub = NULL;
    ok (rutil_idset_diff (ids1, ids2, &add, &sub) == 0
        && add == NULL
        && sub == NULL,
        "rutil_idset_diff [1-2] [1-2] sets add=NULL sub=NULL");
    idset_destroy (add);
    idset_destroy (sub);

    idset_destroy (ids1);
    idset_destroy (ids2);
}

void test_set_json_idset (void)
{
    json_t *o;
    json_t *o2;
    const char *s;
    struct idset *ids;

    if (!(ids= idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (idset_set (ids, 42) < 0)
        BAIL_OUT ("idset_set failed");

    if (!(o = json_object ()))
        BAIL_OUT ("json_object failed");

    errno = 0;
    ok (rutil_set_json_idset (NULL, "foo", NULL) < 0 && errno == EINVAL,
        "rutil_set_json_idset obj=NULL fails with EINVAL");
    errno = 0;
    ok (rutil_set_json_idset (o, NULL, NULL) < 0 && errno == EINVAL,
        "rutil_set_json_idset key=NULL fails with EINVAL");
    errno = 0;
    ok (rutil_set_json_idset (o, "", NULL) < 0 && errno == EINVAL,
        "rutil_set_json_idset key=(empty) fails with EINVAL");

    ok (rutil_set_json_idset (o, "foo", NULL) == 0
            && (o2 = json_object_get (o, "foo"))
            && (s = json_string_value (o2))
            && !strcmp (s, ""),
        "rutil_set_json_idset ids=NULL sets empty string value");
    ok (rutil_set_json_idset (o, "bar", ids) == 0
            && (o2 = json_object_get (o, "bar"))
            && (s = json_string_value (o2))
            && !strcmp (s, "42"),
        "rutil_set_json_idset ids=[42] sets encoded value");

    json_decref (o);
    idset_destroy (ids);
}

void test_idset_decode_test (void)
{
    ok (rutil_idset_decode_test (NULL, 0) == false,
        "rutil_idset_decode_test idset=NULL returns false");
    ok (rutil_idset_decode_test ("", 0) == false,
        "rutil_idset_decode_test idset=\"\" id=0 returns false");
    ok (rutil_idset_decode_test ("0", 0) == true,
        "rutil_idset_decode_test idset=\"0\" id=0 returns true");
    ok (rutil_idset_decode_test ("0", 1) == false,
        "rutil_idset_decode_test idset=\"0\" id=1 returns false");
}

static char *create_tmp_file (const char *content)
{
    char *path;
    char *tmpdir = getenv ("TMPDIR");
    int fd = -1;

    if (!tmpdir)
        tmpdir = "/tmp";
    if (asprintf (&path, "%s/rutil-test.XXXXXX", tmpdir) < 0)
        BAIL_OUT ("error allocating buffer");
    if ((fd = mkostemp (path, O_WRONLY)) < 0)
        BAIL_OUT ("error creating temp file");

    cleanup_push_string (cleanup_file, path);

    if (write_all (fd, content, strlen (content)) < 0)
        BAIL_OUT ("writing to creating temp file");
    close (fd);
    return path;
}

void test_read_file (void)
{
    char ebuf[128];
    char *s;
    char *tmp = create_tmp_file ("XXX");

    errno = 0;
    ebuf[0] = '\0';
    s = rutil_read_file ("/noexist", ebuf, sizeof (ebuf));
    ok (s == NULL && errno == ENOENT && strlen (ebuf) > 0,
         "rutil_read_file path=/noexist fails with ENOENT and human error");
    diag ("%s", ebuf);

    s = rutil_read_file (tmp, ebuf, sizeof (ebuf));
    ok (s != NULL && !strcmp (s, "XXX"),
        "rutil_read_file works");
    free (s);

    free (tmp);
}

void test_load_file (void)
{
    char ebuf[128];
    json_t *o;
    char *good = create_tmp_file ("{\"foo\":42}");
    char *bad = create_tmp_file ("XXX");

    errno = 0;
    ebuf[0] = '\0';
    o = rutil_load_file ("/noexist", ebuf, sizeof (ebuf));
    ok (o == NULL && errno == ENOENT && strlen (ebuf) > 0,
         "rutil_load_file path=/noexist fails with ENOENT and human error");
    diag ("%s", ebuf);

    errno = 0;
    ebuf[0] = '\0';
    o = rutil_load_file (bad, ebuf, sizeof (ebuf));
    ok (o == NULL && errno != 0 && strlen (ebuf) > 0,
         "rutil_load_file with errno and human error on bad JSON");
    diag ("%s", ebuf);

    o = rutil_load_file (good, ebuf, sizeof (ebuf));
    ok (o != NULL && json_object_get (o, "foo"),
         "rutil_load_file with good JSON works");
    json_decref (o);

    free (good);
    free (bad);
}

static char *create_tmp_xml_dir (int size)
{
    char *path;
    char *tmpdir = getenv ("TMPDIR");
    int i;

    if (!tmpdir)
        tmpdir = "/tmp";
    if (asprintf (&path, "%s/rutil-test.XXXXXX", tmpdir) < 0)
        BAIL_OUT ("error allocating buffer");
    if (!mkdtemp (path))
        BAIL_OUT ("failed to create tmp xmldir");

    cleanup_push_string (cleanup_directory_recursive, path);

    for (i = 0; i < size; i++) {
        char fpath[1024];
        int ffd;
        snprintf (fpath, sizeof (fpath), "%s/%d.xml", path, i);
        ffd = open (fpath, O_WRONLY | O_CREAT, 0644);
        if (ffd < 0)
            BAIL_OUT ("failed to create %s", fpath);
        if (write_all (ffd, "\"foo\"",  5) < 0)
            BAIL_OUT ("failed to write %s", fpath);
        close (ffd);
    }

    return path;
}

void test_load_xml_dir (void)
{
    const int count = 8;
    char *path = create_tmp_xml_dir (count);
    char ebuf[128];
    json_t *o;

    errno = 0;
    ebuf[0] = '\0';
    o = rutil_load_xml_dir ("/noexist", ebuf, sizeof (ebuf));
    ok (o == NULL && errno == ENOENT && strlen (ebuf) > 0,
         "rutil_load_xml_dir path=/noexist fails with ENOENT and human error");
    diag ("%s", ebuf);

    o = rutil_load_xml_dir (path, ebuf, sizeof (ebuf));
    ok (o != NULL,
        "rutil_load_xml_dir works");
    if (o) {
        char *tmp = json_dumps (o, JSON_COMPACT);
        diag ("%s", tmp);
        free (tmp);
    }
    ok (json_object_size (o) == count,
        "and contains the expected number of keys");
    json_decref (o);

    free (path);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_match_request_sender ();
    test_idset_sub ();
    test_idset_add ();
    test_idset_diff ();
    test_set_json_idset ();
    test_idset_decode_test ();

    test_read_file ();
    test_load_file ();
    test_load_xml_dir ();

    done_testing ();
    return (0);
}


/*
 * vi:ts=4 sw=4 expandtab
 */
