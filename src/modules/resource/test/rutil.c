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
#include "ccan/str/str.h"

#include "src/modules/resource/rutil.h"

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
            && streq (s, ""),
        "rutil_set_json_idset ids=NULL sets empty string value");
    ok (rutil_set_json_idset (o, "bar", ids) == 0
            && (o2 = json_object_get (o, "bar"))
            && (s = json_string_value (o2))
            && streq (s, "42"),
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
    flux_error_t error;
    char *s;
    char *tmp = create_tmp_file ("XXX");

    errno = 0;
    error.text[0] = '\0';
    s = rutil_read_file ("/noexist", &error);
    ok (s == NULL && errno == ENOENT && strlen (error.text) > 0,
         "rutil_read_file path=/noexist fails with ENOENT and human error");
    diag ("%s", error.text);

    s = rutil_read_file (tmp, &error);
    ok (s != NULL && streq (s, "XXX"),
        "rutil_read_file works");
    free (s);

    free (tmp);
}

void test_load_file (void)
{
    flux_error_t error;
    json_t *o;
    char *good = create_tmp_file ("{\"foo\":42}");
    char *bad = create_tmp_file ("XXX");

    errno = 0;
    error.text[0] = '\0';
    o = rutil_load_file ("/noexist", &error);
    ok (o == NULL && errno == ENOENT && strlen (error.text) > 0,
         "rutil_load_file path=/noexist fails with ENOENT and human error");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    o = rutil_load_file (bad, &error);
    ok (o == NULL && errno != 0 && strlen (error.text) > 0,
         "rutil_load_file with errno and human error on bad JSON");
    diag ("%s", error.text);

    o = rutil_load_file (good, &error);
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
    flux_error_t error;
    json_t *o;

    errno = 0;
    error.text[0] = '\0';
    o = rutil_load_xml_dir ("/noexist", &error);
    ok (o == NULL && errno == ENOENT && strlen (error.text) > 0,
         "rutil_load_xml_dir path=/noexist fails with ENOENT and human error");
    diag ("%s", error.text);

    o = rutil_load_xml_dir (path, &error);
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

void diag_obj (const char *prefix, json_t *obj)
{
    char *s;
    s = json_dumps (obj, JSON_COMPACT);
    diag ("%s: %s", prefix, s ? s : "fail");
    free (s);
}

static int map_exit_iter;
int mapit (unsigned int id, json_t *val, void *arg)
{
    int *map_count = arg;
    if (map_exit_iter != -1 && *map_count == map_exit_iter) {
        errno = 123456;
        return -1;
    }
    (*map_count)++;
    return 0;
}


void test_idkey_basic (void)
{
    json_t *obj;
    json_t *obj2;
    json_t *val1;
    json_t *val2;
    json_t *val3;
    int map_count;

    if (!(obj = json_object ()))
        BAIL_OUT ("json_object failed");
    if (!(obj2 = json_object ()))
        BAIL_OUT ("json_object failed");
    if (!(val1 = json_pack ("{s:s s:i}", "foo", "xyz", "bar", 42)))
        BAIL_OUT ("json_pack failed");
    if (!(val2 = json_pack ("{s:s s:i}", "foo", "xyz", "bar", 43)))
        BAIL_OUT ("json_pack failed");
    if (!(val3 = json_pack ("{s:s s:i}", "foo", "ZZZ", "bar", 42)))
        BAIL_OUT ("json_pack failed");

    ok (rutil_idkey_insert_id (obj, 0, val1) == 0,
        "rutil_idkey_insert_id 0=val1 works");
    ok (rutil_idkey_insert_id (obj, 1, val2) == 0,
        "rutil_idkey_insert_id 1=val2 works");
    ok (rutil_idkey_insert_id (obj, 2, val1) == 0,
        "rutil_idkey_insert_id 2=val1 works");
    ok (rutil_idkey_insert_id (obj, 3, val2) == 0,
        "rutil_idkey_insert_id 3=val2 works");

    diag_obj ("obj", obj);

    ok (json_object_size (obj) == 2,
        "identical objects were compressed -> 2 keys");

    ok (rutil_idkey_insert_id (obj, 0, val3) == 0,
        "rutil_idkey_insert_id 0=val3 works");
    ok (json_object_size (obj) == 3,
        "object update caused split -> 3 keys");

    map_count = 0;
    map_exit_iter = -1;
    ok (rutil_idkey_map (obj, mapit, &map_count) == 0 && map_count == 4,
        "rutil_idkey_map called once per id (there are %d)", map_count);

    ok (rutil_idkey_count (obj) == 4,
        "rutil_idkey_count agrees");

    map_count = 0;
    map_exit_iter = 1;
    errno = 0;
    ok (rutil_idkey_map (obj, mapit, &map_count) < 0 && map_count == 1
        && errno == 123456,
        "rutil_idkey_map fails when map function returns -1 with errno set");
    map_exit_iter = -1;

    ok (rutil_idkey_merge (obj2, obj) == 0
        && rutil_idkey_count (obj2) == 4
        && json_object_size (obj) == 3,
        "rutil_idkey_merge into empty object works");

    ok (rutil_idkey_merge (obj2, obj) == 0
        && rutil_idkey_count (obj2) == 4
        && json_object_size (obj) == 3,
        "rutil_idkey_merge again has no effect");

    json_decref (obj);
    json_decref (obj2);
    json_decref (val1);
    json_decref (val2);
    json_decref (val3);
}

struct idkey {
    const char *ids;
    const char *val;
    int ids_count;
    int obj_count;
};

struct idkey testinput[] = {
    { "0",          "{\"a\": 0}", 1, 1 },
    { "1",          "{\"a\": 1}", 2, 2 },
    { "2",          "{\"a\": 0}", 3, 2 },
    { "3",          "{\"a\": 1}", 4, 2 },
    { "3-15",       "{\"a\": 2}", 16, 3 },
    { "10-12",      "{\"a\": 3}", 16, 4 },
    { "10-12",      "{\"a\": 2}", 16, 3 },
    { "3-15",       "{\"a\": 1}", 16, 2 },
    { "0-15",       "{\"a\": 4}", 16, 1 },
    { "8-1023",     "{\"a\": 5}", 1024, 2 },
    { "0-48",       "{\"a\": 6}", 1024, 2 },
    { 0 },
};

void test_idkey_one (json_t *obj, struct idkey *idk)
{
    json_t *val;
    int rc;
    json_t *orig;

    if (!(orig = json_deep_copy (obj)))
        BAIL_OUT ("json_deep_copy failed");
    if (!(val = json_loads (idk->val, 0, NULL)))
        BAIL_OUT ("json_loads %s failed", idk->val);
    if (strlen (idk->ids) == 1) {
        unsigned long id = strtoul (idk->ids, NULL, 10);
        rc = rutil_idkey_insert_id (obj, id, val);
    }
    else {
        struct idset *ids = idset_decode (idk->ids);
        if (!ids)
            BAIL_OUT ("idset_decode %s failed", idk->ids);
        rc = rutil_idkey_insert_idset (obj, ids, val);
        idset_destroy (ids);
    }
    int ids_count = rutil_idkey_count (obj);
    int obj_count = json_object_size (obj);
    ok (rc == 0 && ids_count == idk->ids_count && obj_count == idk->obj_count,
        "rutil_idkey_insert \"%s\":%s: %d obj %d ids",
        idk->ids, idk->val, idk->ids_count, idk->obj_count);
    if (rc != 0 || ids_count != idk->ids_count || obj_count != idk->obj_count){
        diag_obj ("before", orig);
        diag ("rc=%d ids_count=%d obj_count=%d", rc, ids_count, obj_count);
        diag_obj ("after", obj);
    }

    json_decref (val);
    json_decref (orig);
}

void test_idkey (void)
{
    json_t *obj;
    int i;

    if (!(obj = json_object ()))
        BAIL_OUT ("json_object failed");

    for (i = 0; testinput[i].ids != NULL; i++)
        test_idkey_one (obj, &testinput[i]);
    json_decref (obj);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_idset_diff ();
    test_set_json_idset ();
    test_idset_decode_test ();

    test_read_file ();
    test_load_file ();
    test_load_xml_dir ();

    test_idkey_basic ();
    test_idkey ();

    done_testing ();
    return (0);
}


/*
 * vi:ts=4 sw=4 expandtab
 */
