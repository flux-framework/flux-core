/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <czmq.h>

#include "src/common/libutil/zsecurity.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"

void test_ctor_dtor (void)
{
    zsecurity_t *sec;
    const char *s;

    lives_ok ({ zsecurity_destroy (NULL); },
              "zsecurity_destroy accepts a NULL argument");

    ok ((sec = zsecurity_create (0, "/tmp")) != NULL,
        "zsecurity_create with no selected method works");
    ok ((s = zsecurity_errstr (sec)) != NULL && !strcmp (s, "Success"),
        "zsecurity_errstr returns 'Success'");
    ok ((s = zsecurity_get_directory (sec)) != NULL && !strcmp (s, "/tmp"),
        "zsecurity_get_directory returns configured confdir");
    ok (zsecurity_type_enabled (sec, ZSECURITY_TYPE_PLAIN) == false,
        "zsecurity_type_enabled ZSECURITY_TYPE_PLAIN false");
    ok (zsecurity_type_enabled (sec, ZSECURITY_TYPE_CURVE) == false,
        "zsecurity_type_enabled ZSECURITY_TYPE_CURVE false");
    zsecurity_destroy (sec);

    ok ((sec = zsecurity_create (0, NULL)) != NULL,
        "zsecurity_create with NULL confdir works");
    ok (zsecurity_get_directory (sec) == NULL,
        "zsecurity_get_directory returns configured NULL");
    zsecurity_destroy (sec);

    errno = 0;
    sec = zsecurity_create (ZSECURITY_TYPE_CURVE | ZSECURITY_TYPE_PLAIN, NULL);
    ok (sec == NULL && errno == EINVAL,
        "zsecurity_create PLAIN|CURVE returns EINVAL");

    ok ((sec = zsecurity_create (ZSECURITY_TYPE_PLAIN, NULL)) != NULL,
        "zsecurity_create PLAIN works");
    ok (zsecurity_type_enabled (sec, ZSECURITY_TYPE_PLAIN) == true,
        "zsecurity_type_enabled ZSECURITY_TYPE_PLAIN true");
    ok (zsecurity_type_enabled (sec, ZSECURITY_TYPE_CURVE) == false,
        "zsecurity_type_enabled ZSECURITY_TYPE_CURVE false");
    zsecurity_destroy (sec);
}

void test_keygen (void)
{
    zsecurity_t *sec;
    const char *tmp = getenv ("TMPDIR");
    char path[PATH_MAX];
    struct stat sb;

    /* NULL confdir.
     */
    sec = zsecurity_create (0, NULL);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    errno = 0;
    ok (zsecurity_keygen (sec) < 0 && errno == EINVAL,
        "zsecurity_keygen fails with EINVAL if confdir not set");
    zsecurity_destroy (sec);

    /* Nonexistent confdir.
     *
     * errno has multiple possibilities depending on system, EACCES,
     * EROFS, EPERM, etc.  Simply check for failure and errno != 0.
     */
    sec = zsecurity_create (0, "/noexist");
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    errno = 0;
    ok (zsecurity_keygen (sec) < 0 && errno != 0,
        "zsecurity_keygen fails with errno != 0 if confdir does not exist");
    zsecurity_destroy (sec);

    /* Same with FORCE flag.
     */
    sec = zsecurity_create (ZSECURITY_KEYGEN_FORCE, "/noexist");
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    errno = 0;
    ok (zsecurity_keygen (sec) < 0 && errno != 0,
        "zsecurity_keygen (force) fails with errno != 0 if confdir does not "
        "exist");
    zsecurity_destroy (sec);

    /* No security modes selected.
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (0, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    ok (zsecurity_keygen (sec) == 0,
        "zsecurity_keygen with no security modes works");
    ok ((stat (path, &sb) == 0 && S_ISDIR (sb.st_mode)
         && (sb.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == 0700),
        "confdir is a directory with mode 0700");
    ok (unlink_recursive (path) == 1, "unlinked 1 file/dir");
    zsecurity_destroy (sec);

    /* Wrong confdir perms
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (0, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    if (chmod (path, 0755) < 0)
        BAIL_OUT ("chmod %s: %s", path, strerror (errno));
    errno = 0;
    ok (zsecurity_keygen (sec) < 0 && errno == EPERM,
        "zsecurity_keygen with bad mode confdir fails with EPERM");
    ok (unlink_recursive (path) == 1, "unlinked 1 file/dir");
    zsecurity_destroy (sec);

    /* PLAIN
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (ZSECURITY_TYPE_PLAIN, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    ok (zsecurity_keygen (sec) == 0, "zsecurity_keygen PLAIN works");
    ok (unlink_recursive (path) == 2, "unlinked 2 file/dir");
    zsecurity_destroy (sec);

    /* CURVE
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (ZSECURITY_TYPE_CURVE, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    ok (zsecurity_keygen (sec) == 0, "zsecurity_keygen CURVE works");
    ok (unlink_recursive (path) == 6, "unlinked 6 file/dir");
    zsecurity_destroy (sec);

    /* CURVE overwrite
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (ZSECURITY_TYPE_CURVE, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    if (zsecurity_keygen (sec) < 0)
        BAIL_OUT ("zsecurity_keygen CURVE failed");
    errno = 0;
    ok (zsecurity_keygen (sec) < 0 && errno == EEXIST,
        "zsecurity_keygen CURVE-overwrite fails with EEXIST");
    ok (unlink_recursive (path) == 6, "unlinked 6 file/dir");
    zsecurity_destroy (sec);

    /* Same with FORCE
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec =
        zsecurity_create (ZSECURITY_TYPE_CURVE | ZSECURITY_KEYGEN_FORCE, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    if (zsecurity_keygen (sec) < 0)
        BAIL_OUT ("zsecurity_keygen CURVE failed");
    errno = 0;
    ok (zsecurity_keygen (sec) == 0,
        "zsecurity_keygen (force) CURVE-overwrite works");
    ok (unlink_recursive (path) == 6, "unlinked 6 file/dir");
    zsecurity_destroy (sec);

    /* PLAIN overwrite
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (ZSECURITY_TYPE_PLAIN, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    if (zsecurity_keygen (sec) < 0)
        BAIL_OUT ("zsecurity_keygen PLAIN failed");
    errno = 0;
    ok (zsecurity_keygen (sec) < 0 && errno == EEXIST,
        "zsecurity_keygen PLAIN-overwrite fails with EEXIST");
    ok (unlink_recursive (path) == 2, "unlinked 2 file/dir");
    zsecurity_destroy (sec);

    /* Same with FORCE
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec =
        zsecurity_create (ZSECURITY_TYPE_PLAIN | ZSECURITY_KEYGEN_FORCE, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create failed");
    if (zsecurity_keygen (sec) < 0)
        BAIL_OUT ("zsecurity_keygen PLAIN failed");
    errno = 0;
    ok (zsecurity_keygen (sec) == 0,
        "zsecurity_keygen (force) PLAIN-overwrite works");
    ok (unlink_recursive (path) == 2, "unlinked 2 file/dir");
    zsecurity_destroy (sec);
}

void test_plain (void)
{
    zsecurity_t *sec;
    const char *tmp = getenv ("TMPDIR");
    char path[PATH_MAX];
    zsock_t *cli, *srv, *rdy, *rogue;
    zpoller_t *srv_poller;
    int srv_port;
    char *s;

    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (ZSECURITY_TYPE_PLAIN | ZSECURITY_VERBOSE, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create PLAIN failed");
    if (zsecurity_keygen (sec) < 0)
        BAIL_OUT ("zsecurity_keygen PLAIN failed");
    ok (zsecurity_comms_init (sec) == 0, "zsecurity_comms_init PLAIN works");

    /* set up server */
    if (!(srv = zsock_new_pull (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsecurity_ssockinit (sec, srv) == 0, "zsecurity_ssockinit works");
    srv_port = zsock_bind (srv, "tcp://127.0.0.1:*");
    ok (srv_port >= 0, "server bound to localhost on port %d", srv_port);
    if (!(srv_poller = zpoller_new (srv, NULL)))
        BAIL_OUT ("poller_new failed");

    /* set up client */
    if (!(cli = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsecurity_csockinit (sec, cli) == 0, "zsecurity_csockinit works");
    ok (zsock_connect (cli, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "client connected to server");
    ok (zstr_sendx (cli, "Hi", NULL) == 0, "client sent Hi");
    rdy = zpoller_wait (srv_poller, 1000);
    ok (rdy == srv, "server ready within 1s timeout");
    s = NULL;
    ok (rdy != NULL && zstr_recvx (srv, &s, NULL) == 1 && s != NULL
            && !strcmp (s, "Hi"),
        "server received Hi");
    free (s);

    /* rogue client tries to send with no security setup */
    if (!(rogue = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsock_connect (rogue, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "rogue connected to server with no security");
    ok (zstr_sendx (rogue, "Blimey!", NULL) == 0, "rogue sent Blimey!");
    rdy = zpoller_wait (srv_poller, 200);
    ok (rdy == NULL && zpoller_expired (srv_poller),
        "server not ready within 0.2s timeout");
    zsock_destroy (&rogue);

    /* rogue client tries to send with wrong PLAIN password */
    if (!(rogue = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    zsock_set_plain_username (rogue, "client");
    zsock_set_plain_password (rogue, "not-the-correct-password");
    ok (zsock_connect (rogue, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "rogue connected to server using wrong password");
    ok (zstr_sendx (rogue, "Skallywag!", NULL) == 0, "rogue sent Skallywag!");
    rdy = zpoller_wait (srv_poller, 200);
    ok (rdy == NULL && zpoller_expired (srv_poller),
        "server not ready within 0.2s timeout");
    zsock_destroy (&rogue);

    zsock_destroy (&cli);
    zpoller_destroy (&srv_poller);
    zsock_destroy (&srv);
    zsecurity_destroy (sec);
    unlink_recursive (path);
}

void test_curve (void)
{
    zsecurity_t *sec;
    const char *tmp = getenv ("TMPDIR");
    char path[PATH_MAX];
    zsock_t *cli, *srv, *rdy, *rogue;
    zpoller_t *srv_poller;
    int srv_port;
    char *s;
    zcert_t *rogue_cert;

    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = zsecurity_create (ZSECURITY_TYPE_CURVE | ZSECURITY_VERBOSE, path);
    if (!sec)
        BAIL_OUT ("zsecurity_create CURVE failed");
    if (zsecurity_keygen (sec) < 0)
        BAIL_OUT ("zsecurity_keygen CURVE failed");
    ok (zsecurity_comms_init (sec) == 0, "zsecurity_comms_init CURVE works");

    /* set up server */
    if (!(srv = zsock_new_pull (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsecurity_ssockinit (sec, srv) == 0, "zsecurity_ssockinit works");
    srv_port = zsock_bind (srv, "tcp://127.0.0.1:*");
    ok (srv_port >= 0, "server bound to localhost on port %d", srv_port);
    if (!(srv_poller = zpoller_new (srv, NULL)))
        BAIL_OUT ("poller_new failed");

    /* set up client */
    if (!(cli = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsecurity_csockinit (sec, cli) == 0, "zsecurity_csockinit works");
    ok (zsock_connect (cli, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "client connected to server");

    /* client sends Greetings! */
    ok (zstr_sendx (cli, "Greetings!", NULL) == 0, "client sent Greetings!");
    rdy = zpoller_wait (srv_poller, 1000);
    ok (rdy == srv, "server ready within 1s timeout");
    s = NULL;
    ok (rdy != NULL && zstr_recvx (srv, &s, NULL) == 1 && s != NULL
            && !strcmp (s, "Greetings!"),
        "server received Greetings!");
    free (s);

    /* rogue client tries to send with no security setup */
    if (!(rogue = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsock_connect (rogue, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "rogue connected to server with no security");
    ok (zstr_sendx (rogue, "Avast!", NULL) == 0, "rogue sent Avast");
    rdy = zpoller_wait (srv_poller, 200);
    ok (rdy == NULL && zpoller_expired (srv_poller),
        "server not ready within 0.2s timeout");
    zsock_destroy (&rogue);

    /* rogue client tries to send with correct server public key,
     * but unknown client (server doesn't have public key in "certstore")
     */
    if (!(rogue_cert = zcert_new ()))
        BAIL_OUT ("zcert_new: %s", zmq_strerror (errno));
    if (!(rogue = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    zsock_set_zap_domain (rogue, "flux");  // same as zsecurity_t hardwired
    zcert_apply (rogue_cert, rogue);
    /* read server public key from file */
    char server_file[PATH_MAX];
    int n;
    n = snprintf (server_file, sizeof (server_file), "%s/curve/server", path);
    if ((n < 0) || (n >= sizeof (server_file)))
        BAIL_OUT ("snprintf failed in creation of server_file");
    zcert_t *server_cert = zcert_load (server_file);
    if (!server_cert)
        BAIL_OUT ("zcert_load %s: %s", server_file, zmq_strerror (errno));
    const char *server_key = zcert_public_txt (server_cert);
    zsock_set_curve_serverkey (rogue, server_key);
    /* now connect */
    ok (zsock_connect (rogue, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "rogue connected to server using right server, wrong client key");
    ok (zstr_sendx (rogue, "Haar!", NULL) == 0, "rogue sent Haar!");
    rdy = zpoller_wait (srv_poller, 200);
    ok (rdy == NULL && zpoller_expired (srv_poller),
        "server not ready within 0.2s timeout");
    zcert_destroy (&rogue_cert);
    zcert_destroy (&server_cert);
    zsock_destroy (&rogue);

    zsock_destroy (&cli);
    zpoller_destroy (&srv_poller);
    zsock_destroy (&srv);
    zsecurity_destroy (sec);
    unlink_recursive (path);
}

void alarm_callback (int arg)
{
    diag ("test timed out");
    exit (1);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    signal (SIGALRM, alarm_callback);
    alarm (30);

    test_ctor_dtor ();
    test_keygen ();
    test_plain ();
    test_curve ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
