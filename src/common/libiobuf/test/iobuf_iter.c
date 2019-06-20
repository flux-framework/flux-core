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

void write_data (iobuf_t *iob)
{
    ok (!iobuf_write (iob, "mixed1", 1, "aaa", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed1", 2, "bbb", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 1, "cccccc", 6),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 2, "dddddd", 6),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed1", 2, "bbb", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed1", 1, "aaa", 3),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 2, "dddddd", 6),
        "iobuf_write works");

    ok (!iobuf_write (iob, "mixed2", 1, "cccccc", 6),
        "iobuf_write works");

}

void check_data (const struct iobuf_data *iobuf_data,
                 const char *stream,
                 int rank,
                 const char *data,
                 int data_len)
{
    ok (!strcmp (iobuf_data->stream, stream),
        "iobuf_data stream correct");
    ok (iobuf_data->rank == rank,
        "iobuf_data rank correct");
    ok (!memcmp (iobuf_data->data, data, data_len),
        "iobuf_data data correct");
    ok (iobuf_data->data_len == data_len,
        "iobuf_data data_len correct");
}

void iter_data (iobuf_t *iob)
{
    const struct iobuf_data *iobuf_data;

    iobuf_data = iobuf_iter_first (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_first success");

    check_data (iobuf_data, "mixed1", 1, "aaa", 3);

    iobuf_data = iobuf_iter_next (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_next success");

    check_data (iobuf_data, "mixed1", 2, "bbb", 3);

    iobuf_data = iobuf_iter_next (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_next success");

    check_data (iobuf_data, "mixed2", 1, "cccccc", 6);

    iobuf_data = iobuf_iter_next (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_next success");

    check_data (iobuf_data, "mixed2", 2, "dddddd", 6);

    iobuf_data = iobuf_iter_next (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_next success");

    check_data (iobuf_data, "mixed1", 2, "bbb", 3);

    iobuf_data = iobuf_iter_next (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_next success");

    check_data (iobuf_data, "mixed1", 1, "aaa", 3);

    iobuf_data = iobuf_iter_next (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_next success");

    check_data (iobuf_data, "mixed2", 2, "dddddd", 6);

    iobuf_data = iobuf_iter_next (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_next success");

    check_data (iobuf_data, "mixed2", 1, "cccccc", 6);

    /* iter_first takes us back to the beginning */

    iobuf_data = iobuf_iter_first (iob);
    ok (iobuf_data != NULL,
        "iobuf_iter_first success");

    check_data (iobuf_data, "mixed1", 1, "aaa", 3);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    iobuf_t *iob;

    plan (NO_PLAN);

    /* N.B. flux handle necessary so iobuf can setup rpc message
     * handlers, but it and reactor are unused in these tests */

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);

    ok ((h = flux_open ("loop://", 0)) != NULL,
        "flux_open success");

    ok ((iob = iobuf_server_create (h,
                                    "iobuf-iter-tests",
                                    0,
                                    IOBUF_FLAG_LOG_ERRORS)) != NULL,
        "iobuf_server_create success");

    ok (iobuf_iter_first (iob) == NULL,
        "iobuf_iter_first returns NULL on empty data");
    ok (iobuf_iter_next (iob) == NULL,
        "iobuf_iter_next returns NULL on empty data");

    write_data (iob);

    iter_data (iob);

    done_testing ();

    iobuf_server_destroy (iob);
    flux_close (h);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
