/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <assert.h>
#include <jansson.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libiobuf/iobuf.h"

int maxbuffers = 32;
int streamrank_count = 0;
int eof_count = 0;

void basic_corner_case (void)
{
    errno = 0;
    ok (iobuf_server_create (NULL, NULL, -1, -1) == NULL
        && errno == EINVAL,
        "iobuf_server_create returns EINVAL on bad input");

    /* can pass NULL to destroy */
    iobuf_server_destroy (NULL);

    errno = 0;
    ok (iobuf_set_eof_count_cb (NULL, -1, NULL, NULL) < 0
        && errno == EINVAL,
        "iobuf_set_eof_count_cb returns EINVAL on bad input");

    errno = 0;
    ok (iobuf_create (NULL, NULL, -1) < 0
        && errno == EINVAL,
        "iobuf_create returns EINVAL on bad input");

    errno = 0;
    ok (iobuf_write (NULL, NULL, -1, NULL, -1) < 0
        && errno == EINVAL,
        "iobuf_write returns EINVAL on bad input");

    errno = 0;
    ok (iobuf_read (NULL, NULL, -1, NULL, NULL) < 0
        && errno == EINVAL,
        "iobuf_read returns EINVAL on bad input");

    errno = 0;
    ok (iobuf_eof (NULL, NULL, -1) < 0
        && errno == EINVAL,
        "iobuf_eof returns EINVAL on bad input");

    errno = 0;
    ok (!iobuf_iter_first (NULL)
        && errno == EINVAL,
        "iobuf_iter_first returns EINVAL on bad input");

    errno = 0;
    ok (!iobuf_iter_next (NULL)
        && errno == EINVAL,
        "iobuf_iter_next returns EINVAL on bad input");

    errno = 0;
    ok (iobuf_count (NULL) < 0
        && errno == EINVAL,
        "iobuf_count returns EINVAL on bad input");

    errno = 0;
    ok (iobuf_eof_count (NULL) < 0
        && errno == EINVAL,
        "iobuf_eof_count returns EINVAL on bad input");

    errno = 0;
    ok (!iobuf_rpc_write (NULL, NULL, -1, NULL, -1, NULL, -1)
        && errno == EINVAL,
        "iobuf_rpc_write returns EINVAL on bad input");

    errno = 0;
    ok (!iobuf_rpc_read (NULL, NULL, -1, NULL, -1)
        && errno == EINVAL,
        "iobuf_rpc_read returns EINVAL on bad input");

    errno = 0;
    ok (iobuf_rpc_read_get (NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "iobuf_rpc_read_get returns EINVAL on bad input");

    errno = 0;
    ok (!iobuf_rpc_eof (NULL, NULL, -1, NULL, -1)
        && errno == EINVAL,
        "iobuf_rpc_eof returns EINVAL on bad input");
}

void check_counts (iobuf_t *iob)
{
    int tmp;

    tmp = iobuf_count (iob);
    ok (!(tmp < 0),
        "iobuf_count works");

    ok (tmp == streamrank_count,
        "iobuf_count correct %d == %d",
        tmp, streamrank_count);

    tmp = iobuf_eof_count (iob);
    ok (!(tmp < 0),
        "iobuf_eof_count");

    ok (tmp == eof_count,
        "iobuf_eof_count correct %d == %d",
        tmp, eof_count);
}

void basic_create_write_read (iobuf_t *iob)
{
    char *data;
    int data_len;

    ok (!iobuf_create (iob, "basic_create_write_read", 1),
        "iobuf_create works");

    ok (!iobuf_write (iob, "basic_create_write_read", 1, "foo", 3),
        "iobuf_write works");

    ok (!iobuf_read (iob, "basic_create_write_read", 1, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "foo", 3),
        "iobuf_read returned correct data");

    ok (data_len == 3,
        "iobuf_read returned correct data len");

    free (data);
    streamrank_count++;
    check_counts (iob);
}

void basic_write_read (iobuf_t *iob)
{
    char *data;
    int data_len;

    ok (!iobuf_write (iob, "basic_write_read", 1, "foo", 3),
        "iobuf_write works");

    ok (!iobuf_read (iob, "basic_write_read", 1, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "foo", 3),
        "iobuf_read returned correct data");

    ok (data_len == 3,
        "iobuf_read returned correct data len");

    free (data);
    streamrank_count++;
    check_counts (iob);
}

void basic_write_write_read (iobuf_t *iob)
{
    char *data;
    int data_len;

    ok (!iobuf_write (iob, "basic_write_write_read", 1, "foo", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "basic_write_write_read", 1, "foo", 3),
        "iobuf_write works");

    ok (!iobuf_read (iob, "basic_write_write_read", 1, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "foofoo", 6),
        "iobuf_read returned correct data");

    ok (data_len == 6,
        "iobuf_read returned correct data len");

    free (data);
    streamrank_count++;
    check_counts (iob);
}

void basic_mixed_streams_and_ranks (iobuf_t *iob)
{
    char *data;
    int data_len;

    ok (!iobuf_write (iob, "mixed1", 1, "aaa", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed1", 2, "bbb", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 1, "ccc", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 2, "ddd", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed3", 1, "eee", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed3", 2, "fff", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed1", 2, "bbb", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed3", 2, "fff", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed3", 1, "eee", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed3", 2, "fff", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 1, "ccc", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 2, "ddd", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed3", 1, "eee", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed3", 2, "fff", 3),
        "iobuf_write works");

    ok (!iobuf_read (iob, "mixed1", 1, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "aaa", 3),
        "iobuf_read returned correct data");

    ok (data_len == 3,
        "iobuf_read returned correct data len");

    free (data);

    ok (!iobuf_read (iob, "mixed1", 2, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "bbbbbb", 6),
        "iobuf_read returned correct data");

    ok (data_len == 6,
        "iobuf_read returned correct data len");

    free (data);

    ok (!iobuf_read (iob, "mixed2", 1, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "cccccc", 6),
        "iobuf_read returned correct data");

    ok (data_len == 6,
        "iobuf_read returned correct data len");

    free (data);

    ok (!iobuf_read (iob, "mixed2", 2, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "dddddd", 6),
        "iobuf_read returned correct data");

    ok (data_len == 6,
        "iobuf_read returned correct data len");

    free (data);

    ok (!iobuf_read (iob, "mixed3", 1, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "eeeeeeeee", 9),
        "iobuf_read returned correct data");

    ok (data_len == 9,
        "iobuf_read returned correct data len");

    free (data);

    ok (!iobuf_read (iob, "mixed3", 2, &data, &data_len),
        "iobuf_read works");

    ok (!memcmp (data, "ffffffffffff", 12),
        "iobuf_read returned correct data");

    ok (data_len == 12,
        "iobuf_read returned correct data len");

    free (data);

    streamrank_count+= 6;
    check_counts (iob);
}

void basic_write_eof (iobuf_t *iob)
{
    ok (!iobuf_write (iob, "basic_write_eof", 1, "foo", 3),
        "iobuf_write works");

    streamrank_count++;
    check_counts (iob);

    ok (!iobuf_eof (iob, "basic_write_eof", 1),
        "iobuf_eof works");

    /* double eof shouldn't matter */
    ok (!iobuf_eof (iob, "basic_write_eof", 1),
        "iobuf_eof works");

    eof_count++;
    check_counts (iob);
}

void basic_write_after_eof (iobuf_t *iob)
{
    ok (!iobuf_write (iob, "basic_write_after_eof", 1, "foo", 3),
        "iobuf_write works");

    streamrank_count++;
    check_counts (iob);

    ok (!iobuf_eof (iob, "basic_write_after_eof", 1),
        "iobuf_eof works");

    ok (iobuf_write (iob, "basic_write_after_eof", 1, "foo", 3) < 0
        && errno == EROFS,
        "iobuf_write failed with EROFS");

    eof_count++;
    check_counts (iob);
}

void basic_eof_only (iobuf_t *iob)
{
    ok (!iobuf_eof (iob, "basic_eof_only", 1),
        "iobuf_eof works");

    streamrank_count++;
    eof_count++;
    check_counts (iob);
}

void corner_case_create_twice (iobuf_t *iob)
{
    ok (!iobuf_create (iob, "corner_case_create_twice", 1),
        "iobuf_create works");

    ok (iobuf_create (iob, "corner_case_create_twice", 1) < 0
        && errno == EEXIST,
        "iobuf_create failed with EEXIST");

    streamrank_count++;
    check_counts (iob);
}

void corner_case_invalid_read (iobuf_t *iob)
{
    char *data = NULL;
    int data_len;

    ok (iobuf_read (iob, "corner_case_invalid_read", 1, &data, &data_len) < 0
        && errno == ENOENT,
        "iobuf_read failed with ENOENT");
}

void corner_case_zero_length_write_read (iobuf_t *iob)
{
    char *data;
    int data_len;

    ok (!iobuf_create (iob, "corner_case_zero_length_write_read", 1),
        "iobuf_create works");

    ok (!iobuf_read (iob, "corner_case_zero_length_write_read", 1, &data, &data_len),
        "iobuf_read works");

    ok (data == NULL,
        "iobuf_read returned NULL for zero length read");

    ok (data_len == 0,
        "iobuf_read returned zero for zero length read");

    ok (!iobuf_write (iob, "corner_case_zero_length_write_read", 1, "", 0),
        "iobuf_write works");

    ok (!iobuf_read (iob, "corner_case_zero_length_write_read", 1, &data, &data_len),
        "iobuf_read works");

    ok (data == NULL,
        "iobuf_read returned NULL for zero length read");

    ok (data_len == 0,
        "iobuf_read returned zero for zero length read");

    free (data);
    streamrank_count++;
    check_counts (iob);
}

void corner_case_too_many_buffers (iobuf_t *iob)
{
    int i;

    for (i = streamrank_count; i < maxbuffers; i++) {
        ok (!iobuf_write (iob, "corner_case_too_many_buffers", i + 1, "foo", 3),
            "iobuf_write works");
        streamrank_count++;
    }

    ok (iobuf_write (iob, "corner_case_too_many_buffers", ++i, "foo", 3) < 0
        && errno == ENFILE,
        "iobuf_write failed with ENFILE");
}

int main (int argc, char *argv[])
{
    flux_t *h;
    iobuf_t *iob;

    plan (NO_PLAN);

    basic_corner_case ();

    /* N.B. flux handle necessary so iobuf can setup rpc message
     * handlers, but it and reactor are unused in these tests */

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);

    ok ((h = flux_open ("loop://", 0)) != NULL,
        "flux_open success");

    ok ((iob = iobuf_server_create (h,
                                    "iobuf-tests",
                                    maxbuffers,
                                    IOBUF_FLAG_LOG_ERRORS)) != NULL,
        "iobuf_server_create success");

    basic_create_write_read (iob);
    basic_write_read (iob);
    basic_write_write_read (iob);
    basic_mixed_streams_and_ranks (iob);
    basic_write_eof (iob);
    basic_write_after_eof (iob);
    basic_eof_only (iob);
    corner_case_create_twice (iob);
    corner_case_invalid_read (iob);
    corner_case_zero_length_write_read (iob);
    /* corner_case_too_many_buffers test should be last */
    corner_case_too_many_buffers (iob);

    done_testing ();

    iobuf_server_destroy (iob);
    flux_close (h);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
