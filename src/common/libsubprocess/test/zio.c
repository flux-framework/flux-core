/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjson-c/json.h"

#include "zio.h"

struct counts {
    int close_reader;
    int close_writer;
    int send_reader;
    int fd_read_errors;
    int fd_read_data;
    int fd_read_eof;
};

int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

void fd_read (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct counts *c = arg;
    char buf[64];
    int fd = flux_fd_watcher_get_fd (w);
    int n = -1;

    if ((revents & FLUX_POLLIN)) {
        n = read (fd, buf, sizeof (buf));
        if (n < 0)
            c->fd_read_errors++;
        else if (n == 0) {
            c->fd_read_eof++;
            flux_watcher_stop (w);
        } else if (n > 0)
            c->fd_read_data += n;
    }
    if ((revents & FLUX_POLLERR))
        c->fd_read_errors++;
    diag ("%s: %d", __FUNCTION__, n);
}

int send_reader (zio_t *z, const char *s, int len, void *arg)
{
    struct counts *c = arg;
    c->send_reader++;
    diag ("%s: %*s", __FUNCTION__, len, s);
    return len;
}

int close_reader (zio_t *z, void *arg)
{
    struct counts *c = arg;
    c->close_reader++;
    diag ("%s", __FUNCTION__);
    return 0;
}

int close_writer (zio_t *z, void *arg)
{
    struct counts *c = arg;
    c->close_writer++;
    diag ("%s", __FUNCTION__);
    return 0;
}

void test_encode (void)
{
    int rlen;
    char *r;
    char p[] = "abcdefghijklmnop";
    char q[] = "";
    bool eof;

    char *json = zio_json_encode (p, strlen (p), false);
    if (!json)
        BAIL_OUT ("zio_json_encode failed");
    ok (json != NULL,
        "zio_json_encode works");
    diag ("%s", json);
    r = NULL;
    eof = true;
    rlen = zio_json_decode (json, (void **)&r, &eof);
    ok (rlen == strlen (p) && r != NULL && strcmp (r, p) == 0 && eof == false,
        "zio_json_decode worked");
    free (r);

    eof = true;
    rlen = zio_json_decode (json, NULL, &eof);
    ok (rlen == 0 && eof == false,
        "zio_json_decode worked with NULL data return arg");

    free (json);

    json = zio_json_encode (q, strlen (q), true);
    if (!json)
        BAIL_OUT ("zio_json_encode failed");
    ok (json != NULL,
        "zio_json_encode works on zero length data");
    diag ("%s", json);
    r = NULL;
    eof = false;
    rlen = zio_json_decode (json, (void **)&r, &eof);
    ok (rlen == strlen (q) && r != NULL && strcmp (r, q) == 0 && eof == true,
        "zio_json_decode worked");
    free (r);

    free (json);

    json = zio_json_encode (NULL, 0, true);
    if (!json)
        BAIL_OUT ("zio_json_encode failed");
    ok (json != NULL,
        "zio_json_encode works on NULL data");
    diag ("%s", json);
    rlen = zio_json_decode (json, (void **)&r, NULL);
    ok (rlen == 0 && r != NULL && strcmp (r, q) == 0,
        "zio_json_decode returned empty string");
    free (r);
}

int main (int argc, char **argv)
{
    zio_t *zio;
    int init_fds;
    const char *name;
    struct counts c;
    int fd;
    flux_reactor_t *r;
    flux_watcher_t *w;

    memset (&c, 0, sizeof (c));

    plan (NO_PLAN);

    test_encode ();

    ok ((r = flux_reactor_create (0)) != NULL,
        "flux reactor created");

    init_fds = fdcount ();
    diag ("initial fd count: %d", init_fds);

    /* simple reader tests
     */
    ok ((zio = zio_pipe_reader_create ("test1", &c)) != NULL,
        "reader: zio_pipe_reader_create works");
    ok ((name = zio_name (zio)) != NULL && !strcmp (name, "test1"),
        "reader: zio_name returns correct name");
    ok (zio_set_close_cb (zio, close_reader) == 0,
        "reader: zio_set_close_cb works");
    ok (zio_set_send_cb (zio, send_reader) == 0,
        "reader: zio_set_send_cb works");
    ok (zio_reactor_attach (zio, r) == 0,
        "reader: zio_reactor_attach works");
    ok ((fd = zio_dst_fd (zio)) >= 0,
        "reader: zio_dst_fd returned valid file descriptor");
    ok (write (fd, "narf!", 5) == 5,
        "reader: wrote narf! to reader pipe");
    ok (zio_close_dst_fd (zio) == 0,
        "reader: zio_close_dst_fd succeeded");
    ok (flux_reactor_run (r, 0) == 0,
        "reader: reactor completed successfully");
    ok (c.send_reader == 1,
        "reader: send function called once for EOF + incomplete line");
    errno = 0;
    zio_destroy (zio);
    ok (init_fds == fdcount (),
        "reader: zio_destroy leaks no file descriptors");

    /* simple writer tests
     */
    ok ((zio = zio_pipe_writer_create ("test2", &c)) != NULL,
        "writer: zio_pipe_writer_create works");
    ok ((name = zio_name (zio)) != NULL && !strcmp (name, "test2"),
        "writer: zio_name returns correct name");
    ok (zio_set_close_cb (zio, close_writer) == 0,
        "writer: zio_set_close_cb works");
    ok ((fd = zio_src_fd (zio)) >= 0,
        "writer: zio_src_fd returned valid file descriptor");
    w = flux_fd_watcher_create (r, fd, FLUX_POLLIN, fd_read, &c);
    ok (w != NULL,
        "writer: created fd watcher");
    flux_watcher_start (w);
    ok (zio_write (zio, "narf!", 5) == 5,
        "writer: zio_write narf! works");
    ok (zio_write_eof (zio) == 0,
        "writer: zio_write_eof works");
    ok (flux_reactor_run (r, 0) == 0,
        "writer: reactor completed successfully");
    ok (c.fd_read_errors == 0 && c.fd_read_data == 5 && c.fd_read_eof == 1,
        "writer: read narf + EOF on read end of pipe");
    ok (c.close_writer == 1,
        "writer: close callback invoked");

    zio_destroy (zio);
    ok (init_fds == fdcount (),
        "writer: zio_destroy leaks no file descriptors");

    flux_watcher_destroy (w);
    flux_reactor_destroy (r);

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
