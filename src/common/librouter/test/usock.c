/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/librouter/usock.h"

void tmpdir_destroy (const char *path)
{
    diag ("rm -r %s", path);
    if (unlink_recursive (path) < 0)
        BAIL_OUT ("unlink_recursive failed");
}

void tmpdir_create (char *buf, int size)
{
    const char *tmpdir = getenv ("TMPDIR");

    if (snprintf (buf,
                  size,
                  "%s/usock.XXXXXXX",
                  tmpdir ? tmpdir : "/tmp") >= size)
        BAIL_OUT ("tmpdir_create buffer overflow");
    if (!mkdtemp (buf))
        BAIL_OUT ("mkdtemp %s: %s", buf, strerror (errno));
    diag ("mkdir %s", buf);
}

void touch (const char *path, int mode)
{
    int fd;

    fd = open (path, O_CREAT, mode);
    if (fd < 0 || close (fd) < 0)
        BAIL_OUT ("touch %s: %s", path, strerror (errno));
}

void server_sockpath (const char *tmpdir)
{
    flux_reactor_t *r;
    char path[PATH_MAX + 1];
    struct usock_server *server;
    struct stat sb;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_get_reactor failed");

    /* socket is created with requested mode
     */
    if (snprintf (path, sizeof (path), "%s/usock", tmpdir) >= sizeof (path))
        BAIL_OUT ("buffer overflow");
    server = usock_server_create (r, path, 0600);
    ok (server != NULL,
        "usock_server_create %s works", path);
    ok (stat (path, &sb) == 0
        && S_ISSOCK (sb.st_mode)
        && (sb.st_mode & 0777) == 0600,
        "socket was created with requested mode");
    usock_server_destroy (server);
    errno = 0;
    ok (stat (path, &sb) < 0 && errno == ENOENT,
        "usock_server_destroy unlinked socket");

    /* existing file is clobbered by socket
     */
    touch (path, 0700);
    server = usock_server_create (r, path, 0666);
    ok (server != NULL
        && stat (path, &sb) == 0
        && S_ISSOCK (sb.st_mode)
        && (sb.st_mode & 0777) == 0666,
        "usock_server_create %s clobbers pre-existing reg file", path);
    usock_server_destroy (server);

    flux_reactor_destroy (r);
}

void server_invalid (void)
{
    flux_reactor_t *r;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");

    errno = 0;
    ok (usock_server_create (NULL, "/tmp/foo", 0666) == NULL && errno == EINVAL,
        "usock_server_create r=NULL fails with EINVAL");

    errno = 0;
    ok (usock_server_create (r, NULL, 0666) == NULL && errno == EINVAL,
        "usock_server_create sockpath=NULL fails with EINVAL");

    errno = 0;
    ok (usock_server_stats_get (NULL) == NULL && errno == EINVAL,
        "usock_server_stats_get server=NULL fails with EINVAL");

    flux_reactor_destroy (r);
}

void conn_invalid (void)
{
    struct usock_conn *conn;
    flux_reactor_t *r;
    flux_msg_t *msg;
    int fd[2];

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) < 0)
        BAIL_OUT ("socketpair failed");
    if (!(conn = usock_conn_create (r, fd[0], fd[1])))
        BAIL_OUT ("usock_conn_create failed");
    if (!(msg = flux_request_encode ("foo.bar", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    errno = 0;
    ok (usock_conn_aux_get (NULL, "foo") == NULL && errno == EINVAL,
        "usock_conn_aux_get conn=NULL fails with EINVAL");

    errno = 0;
    ok (usock_conn_aux_set (NULL, "foo", "x", NULL) < 0 && errno == EINVAL,
        "usock_conn_aux_set conn=NULL fails with EINVAL");

    errno = 0;
    ok (usock_conn_send (NULL, msg) < 0 && errno == EINVAL,
        "usock_conn_send conn=NULL fails with EINVAL");
    errno = 0;
    ok (usock_conn_send (conn, NULL) < 0 && errno == EINVAL,
        "usock_conn_send msg=NULL fails with EINVAL");

    errno = 0;
    ok (usock_conn_create (NULL, 0, 0) == NULL && errno == EINVAL,
        "usock_conn_create r=NULL fails with EINVAL");
    errno = 0;
    ok (usock_conn_create (r, -1, 0) == NULL && errno == EINVAL,
        "usock_conn_create infd=-1 fails with EINVAL");
    errno = 0;
    ok (usock_conn_create (r, 0, -1) == NULL && errno == EINVAL,
        "usock_conn_create outfd=-1 fails with EINVAL");

    flux_msg_destroy (msg);
    usock_conn_destroy (conn);
    (void)close (fd[0]);
    (void)close (fd[1]);
    flux_reactor_destroy (r);
}

void client_invalid (void)
{
    struct usock_retry_params retry = USOCK_RETRY_NONE;
    char *longstr;

    if (!(longstr = calloc (1, PATH_MAX + 1)))
        BAIL_OUT ("malloc failed");
    memset (longstr, 'a', PATH_MAX);

    errno = 0;
    ok (usock_client_create (-1) == NULL && errno == EINVAL,
        "usock_client_create fd=-1 fails with EINVAL");

    errno = 0;
    ok (usock_client_connect (NULL, retry) < 0 && errno == EINVAL,
        "usock_client_connect path=NULL fails with EINVAL");

    errno = 0;
    ok (usock_client_connect ("", retry) < 0 && errno == EINVAL,
        "usock_client_connect path=\"\" fails with EINVAL");

    errno = 0;
    ok (usock_client_connect (longstr, retry) < 0 && errno == EINVAL,
        "usock_client_connect path=(longstr) fails with EINVAL");

    errno = 0;
    retry.max_retry = -1;
    ok (usock_client_connect ("foo", retry) < 0 && errno == EINVAL,
        "usock_client_connect max_retry=-1 fails with EINVAL");
    retry.max_retry = 0;

    errno = 0;
    retry.min_delay = -1;
    ok (usock_client_connect ("foo", retry) < 0 && errno == EINVAL,
        "usock_client_connect min_delay=-1 fails with EINVAL");
    retry.min_delay = 0;

    errno = 0;
    retry.max_delay = -1;
    ok (usock_client_connect ("foo", retry) < 0 && errno == EINVAL,
        "usock_client_connect max_delay=-1 fails with EINVAL");
    retry.min_delay = 0;

    free (longstr);
}

void client_connect (void)
{
    struct usock_retry_params retry;

    ok (usock_client_connect ("/noexist", USOCK_RETRY_NONE) < 0
        && errno == ENOENT,
        "usock_client_connect path=/noexist (retry=none) fails with ENOENT");

    ok (usock_client_connect ("/noexist", USOCK_RETRY_DEFAULT) < 0
        && errno == ENOENT,
        "usock_client_connect path=/noexist (retry=default) fails with ENOENT");

    /* hit the retry cap */
    retry.max_retry = 2;
    retry.min_delay = 0.1;
    retry.max_delay = 0.1;
    ok (usock_client_connect ("/noexist", retry) < 0
        && errno == ENOENT,
        "usock_client_connect path=/noexist (retry=capped) fails with ENOENT");
}

int main (int argc, char *argv[])
{
    char tmpdir[PATH_MAX + 1];

    plan (NO_PLAN);
    tmpdir_create (tmpdir, sizeof (tmpdir));

    server_sockpath (tmpdir);

    server_invalid ();
    conn_invalid ();
    client_invalid ();

    client_connect();


    tmpdir_destroy (tmpdir);
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
