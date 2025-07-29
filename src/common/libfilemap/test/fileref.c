/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/libutil/blobref.h"
#include "ccan/array_size/array_size.h"
#include "src/common/libccan/ccan/str/str.h"
#include "ccan/base64/base64.h"

#include "fileref.h"

static char testdir[1024];

static bool have_sparse;

static json_t *xfileref_create_vec (const char *path,
                                    const char *hashtype,
                                    int chunksize)
{
    json_t *o;
    flux_error_t error;
    struct blobvec_param blobvec_param = {
        .hashtype = hashtype,
        .chunksize = chunksize,
        .small_file_threshold = 4096,
    };

    o = fileref_create_ex (path, &blobvec_param, NULL, &error);
    if (!o)
        diag ("%s", error.text);
    return o;
}

static json_t *xfileref_create (const char *path)
{
    json_t *o;
    flux_error_t error;

    o = fileref_create (path, &error);
    if (!o)
        diag ("%s", error.text);
    return o;
}

const char *mkpath (const char *name)
{
    static char tmppath[2048];
    snprintf (tmppath, sizeof (tmppath), "%s/%s", testdir, name);
    return tmppath;
}
const char *mkpath_relative (const char *name)
{
    const char *cp = mkpath (name);
    while (*cp == '/')
        cp++;
    return cp;
}

void rmfile (const char *name)
{
    if (unlink (mkpath (name)) < 0)
        BAIL_OUT ("error unlinking %s", mkpath (name));
}

/* SEEK_DATA support was added to the linux NFS client in kernel 3.18.
 * In el7 based distros, it is defined but doesn't work on NFS.  So ensure
 * SEEK_DATA returns ENXIO on file that is 100% empty.
 */
bool test_sparse (void)
{
    bool result = false;
#ifdef SEEK_DATA
    int fd;
    struct stat sb;

    fd = open (mkpath ("testhole"), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        BAIL_OUT ("error creating test file: %s", strerror (errno));
    if (ftruncate (fd, 8192) < 0)
        BAIL_OUT ("error truncating test file: %s", strerror (errno));
    if (fstat (fd, &sb) < 0)
        BAIL_OUT ("error stating test file: %s", strerror (errno));
    if (sb.st_blocks == 0
        && lseek (fd, 0, SEEK_DATA) == (off_t)-1
        && errno == ENXIO)
        result = true;
    close (fd);
    rmfile ("testhole");
#endif /* SEEK_DATA */
    return result;
}

bool is_sparse (const char *name)
{
    bool result = false;
#ifdef SEEK_HOLE
    int fd;
    struct stat sb;
    off_t offset;

    fd = open (mkpath (name), O_RDONLY);
    if (fd < 0)
        return false;
    if (fstat (fd, &sb) < 0 || !S_ISREG (sb.st_mode))
        goto error;
    offset = lseek (fd, 0, SEEK_HOLE);
    if (offset < sb.st_size)
        result = true;
error:
    close (fd);
#endif /* SEEK_HOLE */
    return result;
}

/* Create test file 'name' under test directory.
 * Each character in  'spec' represents one block filled with that character,
 * except for "-" which tries to create a hole (if supported by file system).
 */
void mkfile (const char *name, int blocksize, const char *spec)
{
    char *buf;
    int fd;
    const char *cp;

    if (!(buf = malloc (blocksize)))
        BAIL_OUT ("malloc failed");
    if ((fd = open (mkpath (name), O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
        BAIL_OUT ("could not create %s: %s", name, strerror (errno));
    for (cp = &spec[0]; *cp != '\0'; cp++) {
        if (*cp == '-') {
            if (lseek (fd, blocksize, SEEK_CUR) == (off_t)-1)
                BAIL_OUT ("error lseeking in %s: %s", name, strerror (errno));
        }
        else {
            memset (buf, *cp, blocksize);
            size_t n = write (fd, buf, blocksize);
            if (n < 0)
                BAIL_OUT ("error writing to %s: %s", name, strerror (errno));
            if (n < blocksize)
                BAIL_OUT ("short write to %s", name);
        }
    }
    if (close (fd) < 0)
        BAIL_OUT ("error closing %s: %s", name, strerror (errno));
    free (buf);
}

void mkfile_string (const char *name, const char *s)
{
    int fd;
    if ((fd = open (mkpath (name), O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0
        || write (fd, s, strlen (s)) != strlen (s)
        || close (fd) < 0)
        BAIL_OUT ("could not create %s: %s", strerror (errno));
    close (fd);
}

void mkfile_empty (const char *name, size_t size)
{
    int fd;
    if ((fd = open (mkpath (name), O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0
        || ftruncate (fd, size) < 0
        || close (fd) < 0)
        BAIL_OUT ("could not create %s: %s", strerror (errno));
    close (fd);
}

/* Check that blobref 'bref' hash matches specified file region.
 * If bref is NULL, check that the region contains all zeroes.
 */
bool check_blob (int fd, off_t offset, size_t size, const char *bref)
{
    char *buf;
    ssize_t n;
    char blobref[BLOBREF_MAX_STRING_SIZE];

    if (!(buf = malloc (size)))
        BAIL_OUT ("out of memory");
    if (lseek (fd, offset, SEEK_SET) == (off_t)-1) {
        diag ("lseek: %s", strerror (errno));
        goto error;
    }
    if ((n = read (fd, buf, size)) < 0) {
        diag ("read: %s", strerror (errno));
        goto error;
    }
    if (n < size) {
        errno = EINVAL;
        diag ("short read");
        goto error;
    }
    if (bref) {
        char hashtype[64], *cp;
        snprintf (hashtype, sizeof (hashtype), "%s", bref);
        if ((cp = strchr (hashtype, '-')))
            *cp = '\0';
        if (blobref_hash (hashtype, buf, size, blobref, sizeof (blobref)) < 0)
            goto error;
        if (!streq (blobref, bref)) {
            diag ("blobref mismatch");
            goto error;
        }
    }
    else { // check hole
        for (int i = 0; i < size; i++) {
            if (buf[i] != 0) {
                diag ("hole mismatch");
                goto error;
            }
        }
    }
    free (buf);
    return true;
error:
    free (buf);
    return false;
}

/* Check that 'fileref' matches the metadata and content of test file 'name'
 * and has the expected blobvec length.
 */
bool check_fileref (json_t *fileref, const char *name, int blobcount)
{
    const char *path;
    int mode;
    json_int_t size = -1;
    json_int_t mtime = -1;
    json_int_t ctime = -1;
    const char *encoding = NULL;
    json_t *data = NULL;
    struct stat sb;
    int myblobcount = 0;

    if (!fileref) {
        diag ("fileref is NULL");
        return false;
    }
    if (json_unpack (fileref,
                     "{s:s s:i s?I s?I s?I s?s s?o}",
                     "path", &path,
                     "mode", &mode,
                     "size", &size,
                     "mtime", &mtime,
                     "ctime", &ctime,
                     "encoding", &encoding,
                     "data", &data) < 0) {

        diag ("error decoding fileref object");
        return false;
    }
    if (!streq (path, mkpath_relative (name))) {
        diag ("fileref.path != expected path");
        return false;
    }
    if (lstat (mkpath (name), &sb) < 0)
        BAIL_OUT ("could not stat %s", path);
    if (size != -1 && size != sb.st_size) {
        diag ("fileref.size is %ju not %zu", (uintmax_t)size, sb.st_size);
        goto error;
    }
    if (mtime != -1 && mtime != sb.st_mtime) {
        diag ("fileref.mtime is wrong");
        goto error;
    }
    if (ctime != -1 && ctime != sb.st_ctime) {
        diag ("fileref.ctime is wrong");
        goto error;
    }
    if (mode != sb.st_mode) {
        diag ("fileref.mode is wrong");
        goto error;
    }
    if (!S_ISREG (mode) && !S_ISDIR (mode) && !S_ISLNK (mode)) {
        diag ("unknown file type");
        goto error;
    }
    if (S_ISLNK (mode)) {
        char buf[1024] = {0};
        if (!data || !json_is_string (data)) {
            diag ("symlink data is missing");
            goto error;
        }
        if (encoding || size != -1) {
            diag ("symlink encoding/size unexpectedly set");
            goto error;
        }
        if (readlink (mkpath (name), buf, sizeof (buf)) < 0
            || !streq (buf, json_string_value (data))) {
            diag ("symlink target is wrong");
            goto error;
        }
    }
    else if (S_ISREG (mode)) {
        if (!encoding) { // json encoding
        }
        else if (streq (encoding, "utf-8")) {
            if (!json_is_string (data)) {
                diag ("regfile utf-8 data is not a string");
                goto error;
            }
        }
        else if (streq (encoding, "base64")) {
            if (!json_is_string (data)) {
                diag ("regfile base64 data is not a string");
                goto error;
            }
        }
        else if (streq (encoding, "blobvec")) {
            if (!json_is_array (data)) {
                diag ("regfile blobvec data is not an array");
                goto error;
            }
            myblobcount = json_array_size (data);
        }
        else {
            diag ("unknown encoding %s", encoding);
            goto error;
        }
    }
    else if (S_ISDIR (mode)) {
        if (data != NULL) {
            diag ("directory has data");
            goto error;
        }
    }
    if (myblobcount != blobcount) {
        diag ("fileref.blobvec has incorrect length (expected %d got %zu)",
              blobcount, myblobcount);
        goto error;
    }
    if (blobcount > 0) {
        int fd;
        size_t index;
        json_t *o;
        off_t cursor;

        if ((fd = open (mkpath (name), O_RDONLY)) < 0) {
            diag ("open %s: %s", path, strerror (errno));
            return false;
        }
        cursor = 0;
        json_array_foreach (data, index, o) {
            struct {
                json_int_t offset;
                json_int_t size;
                const char *blobref;
            } entry;

            if (json_unpack (o, "[I,I,s]",
                             &entry.offset,
                             &entry.size,
                             &entry.blobref) < 0) {
                diag ("failed to unpack blovec entry");
                close (fd);
                goto error;
            }
            // if offset > cursor, we've hit a zero region, check that first
            if (entry.offset > cursor) {
                if (!check_blob (fd, cursor, entry.offset - cursor, NULL)) {
                    diag ("zero region error");
                    close (fd);
                    goto error;
                }
            }
            if (!check_blob (fd, entry.offset, entry.size, entry.blobref)) {
                diag ("content error");
                close (fd);
                goto error;
            }
            cursor = entry.offset + entry.size;
        }
        if (cursor < size) {
            if (!check_blob (fd, cursor, size - cursor, NULL)) {
                diag ("zero region error");
                close (fd);
                goto error;
            }
        }
        close (fd);
    }
    else if (S_ISREG (mode) && data != NULL && encoding
        && streq (encoding, "base64")) {
        const char *str = json_string_value (data);
        int fd;
        char buf[8192];
        char buf2[8192];
        ssize_t n;

        if (!str
            || (n = base64_decode (buf, sizeof (buf), str, strlen (str))) < 0) {
            diag ("base64_decode failed");
            goto error;
        }
        if ((fd = open (mkpath (name), O_RDONLY)) < 0) {
            diag ("open %s: %s", path, strerror (errno));
            goto error;
        }
        if (read (fd, buf2, sizeof (buf2)) != n) {
            diag ("read %s: returned wrong size", path);
            close (fd);
            goto error;
        }
        if (memcmp (buf, buf2, n) != 0) {
            diag ("%s: data is wrong", path);
            close (fd);
            goto error;
        }
        close (fd);
    }
    else if (S_ISREG (mode) && data != NULL
        && encoding && streq (encoding, "utf-8")) {
        const char *str = json_string_value (data);
        ssize_t n = strlen (str);
        int fd;
        char buf[8192];

        if ((fd = open (mkpath (name), O_RDONLY)) < 0) {
            diag ("open %s: %s", path, strerror (errno));
            goto error;
        }
        if (read (fd, buf, sizeof (buf)) != n) {
            diag ("read %s: returned wrong size", path);
            close (fd);
            goto error;
        }
        if (memcmp (str, buf, n) != 0) {
            diag ("%s: data is wrong", path);
            close (fd);
            goto error;
        }
        close (fd);
    }
    return true;
error:
    return false;
}

int blobcount (json_t *fileref)
{
    json_t *o;
    if (json_unpack (fileref, "{s:o}", "blobvec", &o) < 0)
        return -1;
    return json_array_size (o);
}

void diagjson (json_t *o)
{
    char *s = NULL;
    if (o)
        s = json_dumps (o, JSON_INDENT(2));
    diag ("%s", s ? s : "(NULL)");
    free (s);
}

struct testfile {
    const char *spec;
    int chunksize;
    const char *hashtype;
    int exp_blobs;
};

struct testfile testvec[] = {
    { "aaaa", 0, "sha1", 1 },
    { "-aaa", 0, "sha1", 1 },
    { "a-aa", 0, "sha1", 2 },
    { "aaa-", 0, "sha1", 1 },
    { "----", 0, "sha1", 0 },
    { "ac-e--f-", 0, "sha1", 3 },
    { "aaaa", 5500, "sha1", 3 },
    { "aaaa", 8192, "sha1", 2 },
    { "aaaa", 16384, "sha1", 1 },
    { "a--a", 4096, "sha1", 2 },
    { "a--a", 5000, "sha1", 2 },
    { "a--a", 3000, "sha256", 4 },
    { "", 0, "sha1", 0 },
};

void test_vec (void)
{
    for (int i = 0; i < ARRAY_SIZE (testvec); i++) {
        json_t *o;
        bool rc;

        mkfile ("testfile", 4096, testvec[i].spec);
        skip ((strchr (testvec[i].spec, '-')
              && (!have_sparse || !is_sparse ("testfile"))),
              1, "sparse %s test file could not be created", testvec[i].spec);

        o = xfileref_create_vec (mkpath ("testfile"),
                             testvec[i].hashtype,
                             testvec[i].chunksize);
        rc = check_fileref (o, "testfile", testvec[i].exp_blobs);
        ok (rc == true,
            "fileref_create chunksize=%d '%s' works (%d %s blobrefs)",
            testvec[i].chunksize,
            testvec[i].spec,
            testvec[i].exp_blobs,
            testvec[i].hashtype);
        json_decref (o);
        end_skip;
        rmfile ("testfile");
    }
}

void test_dir (void)
{
    json_t *o;
    bool rc;

    if (mkdir (mkpath ("testdir"), 0510) < 0)
        BAIL_OUT ("could not create test directory");
    o = xfileref_create (mkpath ("testdir"));
    diagjson (o);
    rc = check_fileref (o, "testdir", 0);
    ok (rc == true,
        "fileref_create directory works");
    json_decref (o);
    rmdir (mkpath ("testdir"));
}

void test_link (void)
{
    json_t *o;
    const char *target = "/a/b/c/d/e/f/g";
    bool rc;

    if (symlink (target, mkpath ("testlink")) < 0)
        BAIL_OUT ("could not create test symlink");
    o = xfileref_create (mkpath ("testlink"));
    rc = check_fileref (o, "testlink", 0);
    ok (rc == true,
        "fileref_create symlink works");
    json_decref (o);
    rmfile ("testlink");
}

void test_small (void)
{
    json_t *o;
    bool rc;
    const char *encoding;

    mkfile ("testsmall", 512, "a");
    o = xfileref_create_vec (mkpath ("testsmall"), "sha1", 0);
    rc = check_fileref (o, "testsmall", 0);
    ok (rc == true,
        "fileref_create small file works");
    diagjson (o);
    json_decref (o);
    rmfile ("testsmall");

    mkfile_string ("testsmall2", "\xc3\x28");
    o = xfileref_create (mkpath ("testsmall2"));
    ok (o != NULL && json_unpack (o, "{s:s}", "encoding", &encoding) == 0
        && streq (encoding, "base64"),
        "small file with invalid utf-8 encodes as base64");
    diagjson (o);
    json_decref (o);
    rmfile ("testsmall2");

    mkfile_string ("testsmall3", "abcd");
    o = xfileref_create (mkpath ("testsmall3"));
    ok (o != NULL && json_unpack (o, "{s:s}", "encoding", &encoding) == 0
        && streq (encoding, "utf-8"),
        "small file with valid utf-8 encodes as utf-8");
    diagjson (o);
    json_decref (o);
    rmfile ("testsmall3");
}

void test_empty (void)
{
    json_t *o;
    json_int_t size;

    mkfile_empty ("testempty", 0);
    o = xfileref_create (mkpath ("testempty"));
    ok (o != NULL && json_object_get (o, "data") == NULL,
        "empty file has no data member");
    ok (o != NULL && json_object_get (o, "encoding") == NULL,
        "empty file has no encoding member");
    ok (o != NULL && json_unpack (o, "{s:I}", "size", &size) == 0
            && size == 0,
        "empty file has size member set to zero");
    diagjson (o);
    json_decref (o);
    rmfile ("testempty");

    if (!have_sparse) {
        tap_skip (3, "test directory does not support sparse files");
        return;
    }
    mkfile_empty ("testempty2", 1024);
    o = xfileref_create (mkpath ("testempty2"));
    ok (o != NULL && json_object_get (o, "data") == NULL,
        "sparse,empty file has no data member");
    ok (o != NULL && json_object_get (o, "encoding") == NULL,
        "sparse,empty file has no encoding member");
    ok (o != NULL && json_unpack (o, "{s:I}", "size", &size) == 0
            && size == 1024,
        "sparse,empty file has size member set to expected size");
    diagjson (o);
    json_decref (o);
    rmfile ("testempty2");
}

void test_expfail (void)
{
    json_t *o;
    flux_error_t error;
    struct blobvec_param param;

    mkfile ("test", 4096, "zz");

    errno = 0;
    o = fileref_create ("/noexist", &error);
    if (!o)
        diag ("%s", error.text);
    ok (o == NULL && errno == ENOENT,
        "fileref_create path=/noexist fails with ENOENT");

    errno = 0;
    o = fileref_create ("/dev/null", &error);
    if (!o)
        diag ("%s", error.text);
    ok (o == NULL && errno == EINVAL,
        "fileref_create path=/dev/null fails with EINVAL");

    errno = 0;
    param.chunksize = 1024;
    param.hashtype = "smurfette";
    param.small_file_threshold = 0;
    o = fileref_create_ex (mkpath ("test"), &param, NULL, &error);
    if (!o)
        diag ("%s", error.text);
    ok (o == NULL && errno == EINVAL,
        "fileref_create_ex param.hashtype=smurfette fails with EINVAL");

    rmfile ("test");
}

void test_pretty_print (void)
{
    char buf[1024];
    json_t *o;

    mkfile ("testfile", 4096, "a");
    if (!(o = xfileref_create_vec (mkpath ("testfile"), "sha1", 0)))
        BAIL_OUT ("failed to create test object");

    buf[0] = '\0';
    fileref_pretty_print (NULL, NULL, false, buf, sizeof (buf));
    ok (streq (buf, "invalid fileref"),
        "fileref_pretty_print obj=NULL printed an error");

    buf[0] = '\0';
    fileref_pretty_print (NULL, NULL, false, buf, 5);
    ok (streq (buf, "inv+"),
        "fileref_pretty_print obj=NULL bufsize=5 includes trunc character +");

    buf[0] = '\0';
    fileref_pretty_print (o, NULL, false, buf, sizeof (buf));
    ok (strlen (buf) > 0,
        "fileref_pretty_print long_form=false works");
    diag (buf);

    buf[0] = '\0';
    fileref_pretty_print (o, NULL, true, buf, sizeof (buf));
    ok (strlen (buf) > 0,
        "fileref_pretty_print long_form=true works");
    diag (buf);

    lives_ok ({fileref_pretty_print (o, NULL, true, NULL, sizeof (buf));},
             "fileref_pretty_print buf=NULL doesn't crash");

    json_decref (o);
    rmfile ("testfile");
}

int main (int argc, char *argv[])
{
    char *tmpdir = getenv ("TMPDIR");

    plan (NO_PLAN);

    /* create testdir to contain test files
     */
    snprintf (testdir,
              sizeof (testdir),
              "%s/fileref-XXXXXX",
              tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp (testdir))
        BAIL_OUT ("could not create test directory");

    have_sparse = test_sparse ();

    test_vec ();
    test_dir ();
    test_link ();
    test_small ();
    test_empty ();
    test_expfail ();
    test_pretty_print ();

    unlink_recursive (testdir);

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
