/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/fileref.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/libutil/blobref.h"
#include "ccan/array_size/array_size.h"
#include "src/common/libccan/ccan/str/str.h"
#include "ccan/base64/base64.h"

static char testdir[1024];

static bool have_sparse;

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

bool test_sparse (void)
{
    int fd;
    struct stat sb;
    bool result = false;

    fd = open (mkpath ("testhole"), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        BAIL_OUT ("error creating test file: %s", strerror (errno));
    if (ftruncate (fd, 8192) < 0)
        BAIL_OUT ("error truncating test file: %s", strerror (errno));
     if (fstat (fd, &sb) < 0)
        BAIL_OUT ("error stating test file: %s", strerror (errno));
    if (sb.st_blocks == 0)
        result = true;
    close (fd);
    rmfile ("testhole");
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
    int version;
    const char *type;
    const char *path;
    json_int_t size;
    json_int_t mtime;
    json_int_t ctime;
    int mode;
    json_t *blobvec;
    struct stat sb;
    const char *data = NULL;

    if (!fileref) {
        diag ("fileref is NULL");
        return false;
    }
    if (json_unpack (fileref,
                     "{s:i s:s s:s s:I s:I s:I s:i s?s s:o}",
                     "version", &version,
                     "type", &type,
                     "path", &path,
                     "size", &size,
                     "mtime", &mtime,
                     "ctime", &ctime,
                     "mode", &mode,
                     "data", &data,
                     "blobvec", &blobvec) < 0) {
        diag ("error decoding fileref object");
        return false;
    }
    if (version != 1) {
        diag ("fileref.version != 1");
        return false;
    }
    if (!streq (type, "fileref")) {
        diag ("fileref.type != fileref");
        return false;
    }
    if (!streq (path, mkpath_relative (name))) {
        diag ("fileref.path != expected path");
        return false;
    }
    if (lstat (mkpath (name), &sb) < 0)
        BAIL_OUT ("could not stat %s", path);
    if (size != sb.st_size) {
        diag ("fileref.size is %ju not %zu", (uintmax_t)size, sb.st_size);
        goto error;
    }
    if (mtime != sb.st_mtime) {
        diag ("fileref.mtime is wrong");
        goto error;
    }
    if (ctime != sb.st_ctime) {
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
        char buf[1024];
        if (!data) {
            diag ("symlink data is missing");
            goto error;
        }
        if (readlink (mkpath (name), buf, sizeof (buf)) < 0
            || !streq (buf, data)) {
            diag ("symlink target is wrong");
            goto error;
        }
    }
    else if (S_ISREG (mode)) {
        if (data && json_array_size (blobvec) > 0) {
            diag ("regular file has both data and blobrefs");
            goto error;
        }
    }
    else if (S_ISDIR (mode)) {
        if (data != NULL) {
            diag ("directory has data");
            goto error;
        }
    }
    if (json_array_size (blobvec) != blobcount) {
        diag ("fileref.blobvec has incorrect length (expected %d got %zu)",
              blobcount, json_array_size (blobvec));
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
        json_array_foreach (blobvec, index, o) {
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
    else if (S_ISREG (mode) && data != NULL) {
        int fd;
        char buf[8192];
        char buf2[8192];
        ssize_t n;

        if ((n = base64_decode (buf, sizeof (buf), data, strlen (data))) < 0) {
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
    char *s;
    s = json_dumps (o, JSON_INDENT(2));
    if (s)
        diag ("%s", s);
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
        flux_error_t error;
        bool rc;

        skip (strchr (testvec[i].spec, '-') && !have_sparse,
              1, "test directory does not support sparse files");

        mkfile ("testfile", 4096, testvec[i].spec);
        o = fileref_create (mkpath ("testfile"),
                           testvec[i].hashtype,
                           testvec[i].chunksize,
                           &error);
        rc = check_fileref (o, "testfile", testvec[i].exp_blobs);
        ok (rc == true,
            "fileref_create chunksize=%d '%s' works (%d %s blobrefs)",
            testvec[i].chunksize,
            testvec[i].spec,
            testvec[i].exp_blobs,
            testvec[i].hashtype);
        if (!rc)
            diag ("%s", error.text);
        json_decref (o);
        rmfile ("testfile");

        end_skip;
    }
}

void test_dir (void)
{
    json_t *o;
    flux_error_t error;
    bool rc;

    if (mkdir (mkpath ("testdir"), 0510) < 0)
        BAIL_OUT ("could not create test directory");
    o = fileref_create (mkpath ("testdir"), "sha1", 0, &error);
    rc = check_fileref (o, "testdir", 0);
    ok (rc == true,
        "fileref_create directory works");
    if (!rc)
        diag ("%s", error.text);
    json_decref (o);
    rmdir (mkpath ("testdir"));
}

void test_link (void)
{
    json_t *o;
    flux_error_t error;
    const char *target = "/a/b/c/d/e/f/g";
    bool rc;

    if (symlink (target, mkpath ("testlink")) < 0)
        BAIL_OUT ("could not create test symlink");
    o = fileref_create (mkpath ("testlink"), "sha1", 0, &error);
    rc = check_fileref (o, "testlink", 0);
    ok (rc == true,
        "fileref_create symlink works");
    if (!rc)
        diag ("%s", error.text);
    json_decref (o);
    rmfile ("testlink");
}

void test_small (void)
{
    json_t *o;
    flux_error_t error;
    bool rc;

    mkfile ("testsmall", 512, "a");
    o = fileref_create (mkpath ("testsmall"), "sha1", 0, &error);
    rc = check_fileref (o, "testsmall", 0);
    ok (rc == true,
        "fileref_create small file works");
    if (!rc)
        diag ("%s", error.text);
    diagjson (o);
    json_decref (o);
    rmfile ("testsmall");
}

void test_expfail (void)
{
    json_t *o;
    flux_error_t error;

    mkfile ("test", 4096, "zz");

    errno = 0;
    o = fileref_create ("/noexist", "sha1", 4096, &error);
    ok (o == NULL && errno == ENOENT,
        "fileref_create path=/noexist fails with ENOENT");
    if (!o)
        diag ("%s", error.text);

    errno = 0;
    o = fileref_create ("/dev/null", "sha1", 4096, &error);
    ok (o == NULL && errno == EINVAL,
        "fileref_create path=/dev/null fails with EINVAL");
    if (!o)
        diag ("%s", error.text);

    errno = 0;
    o = fileref_create (mkpath ("test"), "smurfette", 4096, &error);
    ok (o == NULL && errno == EINVAL,
        "fileref_create hashtype=smurfette fails with EINVAL");
    if (!o)
        diag ("%s", error.text);

    errno = 0;
    o = fileref_create (mkpath ("test"), "sha1", -1, &error);
    ok (o == NULL && errno == EINVAL,
        "fileref_create chunksize=-1 fails with EINVAL");
    if (!o)
        diag ("%s", error.text);

    rmfile ("test");
}

void test_pretty_print (void)
{
    char buf[1024];
    json_t *o;

    mkfile ("testfile", 4096, "a");
    if (!(o = fileref_create (mkpath ("testfile"), "sha1", 0, NULL)))
        BAIL_OUT ("failed to create test object");

    buf[0] = '\0';
    fileref_pretty_print (NULL, false, buf, sizeof (buf));
    ok (streq (buf, "invalid fileref"),
        "fileref_pretty_print obj=NULL printed an error");

    buf[0] = '\0';
    fileref_pretty_print (NULL, false, buf, 5);
    ok (streq (buf, "inv+"),
        "fileref_pretty_print obj=NULL bufsize=5 includes trunc character +");

    buf[0] = '\0';
    fileref_pretty_print (o, false, buf, sizeof (buf));
    ok (strlen (buf) > 0,
        "fileref_pretty_print long_form=false works");
    diag (buf);

    buf[0] = '\0';
    fileref_pretty_print (o, true, buf, sizeof (buf));
    ok (strlen (buf) > 0,
        "fileref_pretty_print long_form=true works");
    diag (buf);

    lives_ok ({fileref_pretty_print (o, true, NULL, sizeof (buf));},
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
    test_expfail ();
    test_pretty_print ();

    unlink_recursive (testdir);

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
