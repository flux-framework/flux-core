#if HAVE_CONFIG_H
#include "config.h"
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

#include "src/common/libflux/security.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"

void test_ctor_dtor (void)
{
    flux_sec_t *sec;
    const char *s;

    lives_ok ({flux_sec_destroy (NULL);},
            "flux_sec_destroy accepts a NULL argument");

    ok ((sec = flux_sec_create (0, "/tmp")) != NULL,
            "flux_sec_create with no selected method works");
    ok ((s = flux_sec_errstr (sec)) != NULL && !strcmp (s, "Success"),
            "flux_sec_errstr returns 'Success'");
    ok ((s = flux_sec_get_directory (sec)) != NULL && !strcmp (s, "/tmp"),
            "flux_sec_get_directory returns configured confdir");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_PLAIN) == false,
            "flux_sec_type_enabled FLUX_SEC_TYPE_PLAIN false");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_CURVE) == false,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_MUNGE) == false,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    flux_sec_destroy (sec);

    ok ((sec = flux_sec_create (0, NULL)) != NULL,
            "flux_sec_create with NULL confdir works");
    ok (flux_sec_get_directory (sec) == NULL,
            "flux_sec_get_directory returns configured NULL");
    flux_sec_destroy (sec);

    errno = 0;
    sec = flux_sec_create (FLUX_SEC_TYPE_CURVE | FLUX_SEC_TYPE_PLAIN, NULL);
    ok (sec == NULL && errno == EINVAL,
            "flux_sec_create PLAIN|CURVE returns EINVAL");

    ok ((sec = flux_sec_create (FLUX_SEC_TYPE_PLAIN, NULL)) != NULL,
            "flux_sec_create PLAIN works");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_PLAIN) == true,
            "flux_sec_type_enabled FLUX_SEC_TYPE_PLAIN true");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_CURVE) == false,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_MUNGE) == false,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    flux_sec_destroy (sec);

    ok ((sec = flux_sec_create (FLUX_SEC_TYPE_PLAIN | FLUX_SEC_TYPE_MUNGE, NULL)) != NULL,
            "flux_sec_create PLAIN|MUNGE works");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_PLAIN) == true,
            "flux_sec_type_enabled FLUX_SEC_TYPE_PLAIN true");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_CURVE) == false,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_MUNGE) == true,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    flux_sec_destroy (sec);

    ok ((sec = flux_sec_create (FLUX_SEC_TYPE_CURVE | FLUX_SEC_TYPE_MUNGE, NULL)) != NULL,
            "flux_sec_create CURVE|MUNGE works");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_PLAIN) == false,
            "flux_sec_type_enabled FLUX_SEC_TYPE_PLAIN true");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_CURVE) == true,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    ok (flux_sec_type_enabled (sec, FLUX_SEC_TYPE_MUNGE) == true,
            "flux_sec_type_enabled FLUX_SEC_TYPE_CURVE false");
    flux_sec_destroy (sec);
}

void test_keygen (void)
{
    flux_sec_t *sec;
    const char *tmp = getenv ("TMPDIR");
    char path[PATH_MAX];
    struct stat sb;

    /* NULL confdir.
     */
    sec = flux_sec_create (0, NULL);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    errno = 0;
    ok (flux_sec_keygen (sec) < 0 && errno == EINVAL,
            "flux_sec_keygen fails with EINVAL if confdir not set");
    flux_sec_destroy (sec);

    /* Nonexistent confdir.
     *
     * errno has multiple possibilities depending on system, EACCES,
     * EROFS, EPERM, etc.  Simply check for failure and errno != 0.
     */
    sec = flux_sec_create (0, "/noexist");
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    errno = 0;
    ok (flux_sec_keygen (sec) < 0 && errno != 0,
            "flux_sec_keygen fails with errno != 0 if confdir does not exist");
    flux_sec_destroy (sec);

    /* Same with FORCE flag.
     */
    sec = flux_sec_create (FLUX_SEC_KEYGEN_FORCE, "/noexist");
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    errno = 0;
    ok (flux_sec_keygen (sec) < 0 && errno != 0,
            "flux_sec_keygen (force) fails with errno != 0 if confdir does not exist");
    flux_sec_destroy (sec);

    /* No security modes selected.
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (0, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    ok (flux_sec_keygen (sec) == 0,
            "flux_sec_keygen with no security modes works");
    ok ((stat (path, &sb) == 0 && S_ISDIR (sb.st_mode)
                && (sb.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) == 0700),
            "confdir is a directory with mode 0700");
    ok (unlink_recursive (path) == 1,
            "unlinked 1 file/dir");
    flux_sec_destroy (sec);

    /* Wrong confdir perms
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (0, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    if (chmod (path, 0755) < 0)
        BAIL_OUT ("chmod %s: %s", path, strerror (errno));
    errno = 0;
    ok (flux_sec_keygen (sec) < 0 && errno == EPERM,
            "flux_sec_keygen with bad mode confdir fails with EPERM");
    ok (unlink_recursive (path) == 1,
            "unlinked 1 file/dir");
    flux_sec_destroy (sec);

    /* PLAIN
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (FLUX_SEC_TYPE_PLAIN, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    ok (flux_sec_keygen (sec) == 0,
            "flux_sec_keygen PLAIN works");
    ok (unlink_recursive (path) == 2,
            "unlinked 2 file/dir");
    flux_sec_destroy (sec);

    /* CURVE
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (FLUX_SEC_TYPE_CURVE, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    ok (flux_sec_keygen (sec) == 0,
            "flux_sec_keygen CURVE works");
    ok (unlink_recursive (path) == 6,
            "unlinked 6 file/dir");
    flux_sec_destroy (sec);

    /* CURVE overwrite
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (FLUX_SEC_TYPE_CURVE, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    if (flux_sec_keygen (sec) < 0)
        BAIL_OUT ("flux_sec_keygen CURVE failed");
    errno = 0;
    ok (flux_sec_keygen (sec) < 0 && errno == EEXIST,
            "flux_sec_keygen CURVE-overwrite fails with EEXIST");
    ok (unlink_recursive (path) == 6,
            "unlinked 6 file/dir");
    flux_sec_destroy (sec);

    /* Same with FORCE
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (FLUX_SEC_TYPE_CURVE | FLUX_SEC_KEYGEN_FORCE, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    if (flux_sec_keygen (sec) < 0)
        BAIL_OUT ("flux_sec_keygen CURVE failed");
    errno = 0;
    ok (flux_sec_keygen (sec) == 0,
            "flux_sec_keygen (force) CURVE-overwrite works");
    ok (unlink_recursive (path) == 6,
            "unlinked 6 file/dir");
    flux_sec_destroy (sec);

    /* PLAIN overwrite
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (FLUX_SEC_TYPE_PLAIN, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    if (flux_sec_keygen (sec) < 0)
        BAIL_OUT ("flux_sec_keygen PLAIN failed");
    errno = 0;
    ok (flux_sec_keygen (sec) < 0 && errno == EEXIST,
            "flux_sec_keygen PLAIN-overwrite fails with EEXIST");
    ok (unlink_recursive (path) == 2,
            "unlinked 2 file/dir");
    flux_sec_destroy (sec);

    /* Same with FORCE
     */
    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (FLUX_SEC_TYPE_PLAIN | FLUX_SEC_KEYGEN_FORCE, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create failed");
    if (flux_sec_keygen (sec) < 0)
        BAIL_OUT ("flux_sec_keygen PLAIN failed");
    errno = 0;
    ok (flux_sec_keygen (sec) == 0,
            "flux_sec_keygen (force) PLAIN-overwrite works");
    ok (unlink_recursive (path) == 2,
            "unlinked 2 file/dir");
    flux_sec_destroy (sec);
}

void test_munge (void)
{
    flux_sec_t *sec;
    char *cred, *buf;
    size_t credsize, bufsize;

    ok ((sec = flux_sec_create (FLUX_SEC_TYPE_MUNGE, NULL)) != NULL,
            "flux_sec_create MUNGE-real works");
    ok (flux_sec_comms_init (sec) == 0,
            "flux_sec_comms_init MUNGE-real works");
    /* can't test encryption in case munge isn't configured */
    flux_sec_destroy (sec);


    ok ((sec = flux_sec_create (FLUX_SEC_TYPE_MUNGE | FLUX_SEC_FAKEMUNGE,
                                                        NULL)) != NULL,
            "flux_sec_create MUNGE-fake works");
    ok (flux_sec_comms_init (sec) == 0,
            "flux_sec_comms_init MUNGE-fake works");
    ok (flux_sec_csockinit (sec, NULL) == 0,
            "flux_sec_csockinit MUNGE-fake works (no-op)");
    ok (flux_sec_ssockinit (sec, NULL) == 0,
            "flux_sec_ssockinit MUNGE-fake works (no-op)");
    ok (flux_sec_munge (sec, "Hello world", 12, &cred, &credsize) == 0,
            "flux_sec_munge (fake) works");
    ok (flux_sec_unmunge (sec, cred, credsize, &buf, &bufsize) == 0,
            "flux_sec_unmunge (fake) works");
    ok (!strcmp (buf, "Hello world"),
            "unmunge(munge(x))==x");
    free (cred);
    free (buf);
    flux_sec_destroy (sec);
}

void test_plain (void)
{
    flux_sec_t *sec;
    const char *tmp = getenv ("TMPDIR");
    char path[PATH_MAX];
    zsock_t *cli, *srv, *rdy, *rogue;
    zpoller_t *srv_poller;
    int srv_port;
    char *s;

    snprintf (path, sizeof (path), "%s/sectest.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    sec = flux_sec_create (FLUX_SEC_TYPE_PLAIN | FLUX_SEC_VERBOSE, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create PLAIN failed");
    if (flux_sec_keygen (sec) < 0)
        BAIL_OUT ("flux_sec_keygen PLAIN failed");
    ok (flux_sec_comms_init (sec) == 0,
            "flux_sec_comms_init PLAIN works");

    /* set up server */
    if (!(srv = zsock_new_pull (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (flux_sec_ssockinit (sec, srv) == 0,
            "flux_sec_ssockinit works");
    srv_port = zsock_bind (srv, "tcp://127.0.0.1:*");
    ok (srv_port >= 0,
            "server bound to localhost on port %d", srv_port);
    if (!(srv_poller = zpoller_new (srv, NULL)))
        BAIL_OUT ("poller_new failed");

    /* set up client */
    if (!(cli = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (flux_sec_csockinit (sec, cli) == 0,
            "flux_sec_csockinit works");
    ok (zsock_connect (cli, "tcp://127.0.0.1:%d", srv_port) >= 0,
            "client connected to server");
    ok (zstr_sendx (cli, "Hi", NULL) == 0,
            "client sent Hi");
    rdy = zpoller_wait (srv_poller, 1000);
    ok (rdy == srv,
            "server ready within 1s timeout");
    s = NULL;
    ok (rdy != NULL && zstr_recvx (srv, &s, NULL) == 1
            && s != NULL && !strcmp (s, "Hi"),
            "server received Hi");
    free (s);

    /* rogue client tries to send with no security setup */
    if (!(rogue = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsock_connect (rogue, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "rogue connected to server with no security");
    ok (zstr_sendx (rogue, "Blimey!", NULL) == 0,
            "rogue sent Blimey!");
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
    ok (zstr_sendx (rogue, "Skallywag!", NULL) == 0,
            "rogue sent Skallywag!");
    rdy = zpoller_wait (srv_poller, 200);
    ok (rdy == NULL && zpoller_expired (srv_poller),
            "server not ready within 0.2s timeout");
    zsock_destroy (&rogue);

    zsock_destroy (&cli);
    zpoller_destroy (&srv_poller);
    zsock_destroy (&srv);
    flux_sec_destroy (sec);
    unlink_recursive (path);
}

void test_curve (void)
{
    flux_sec_t *sec;
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
    sec = flux_sec_create (FLUX_SEC_TYPE_CURVE | FLUX_SEC_VERBOSE, path);
    if (!sec)
        BAIL_OUT ("flux_sec_create CURVE failed");
    if (flux_sec_keygen (sec) < 0)
        BAIL_OUT ("flux_sec_keygen CURVE failed");
    ok (flux_sec_comms_init (sec) == 0,
            "flux_sec_comms_init CURVE works");

    /* set up server */
    if (!(srv = zsock_new_pull (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (flux_sec_ssockinit (sec, srv) == 0,
            "flux_sec_ssockinit works");
    srv_port = zsock_bind (srv, "tcp://127.0.0.1:*");
    ok (srv_port >= 0,
            "server bound to localhost on port %d", srv_port);
    if (!(srv_poller = zpoller_new (srv, NULL)))
        BAIL_OUT ("poller_new failed");

    /* set up client */
    if (!(cli = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (flux_sec_csockinit (sec, cli) == 0,
            "flux_sec_csockinit works");
    ok (zsock_connect (cli, "tcp://127.0.0.1:%d", srv_port) >= 0,
            "client connected to server");

    /* client sends Greetings! */
    ok (zstr_sendx (cli, "Greetings!", NULL) == 0,
            "client sent Greetings!");
    rdy = zpoller_wait (srv_poller, 1000);
    ok (rdy == srv,
            "server ready within 1s timeout");
    s = NULL;
    ok (rdy != NULL && zstr_recvx (srv, &s, NULL) == 1
            && s != NULL && !strcmp (s, "Greetings!"),
            "server received Greetings!");
    free (s);

    /* rogue client tries to send with no security setup */
    if (!(rogue = zsock_new_push (NULL)))
        BAIL_OUT ("zsock_new: %s", zmq_strerror (errno));
    ok (zsock_connect (rogue, "tcp://127.0.0.1:%d", srv_port) >= 0,
        "rogue connected to server with no security");
    ok (zstr_sendx (rogue, "Avast!", NULL) == 0,
            "rogue sent Avast");
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
    zsock_set_zap_domain (rogue, "flux"); // same as flux_sec_t hardwired
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
    ok (zstr_sendx (rogue, "Haar!", NULL) == 0,
            "rogue sent Haar!");
    rdy = zpoller_wait (srv_poller, 200);
    ok (rdy == NULL && zpoller_expired (srv_poller),
            "server not ready within 0.2s timeout");
    zcert_destroy (&rogue_cert);
    zcert_destroy (&server_cert);
    zsock_destroy (&rogue);

    zsock_destroy (&cli);
    zpoller_destroy (&srv_poller);
    zsock_destroy (&srv);
    flux_sec_destroy (sec);
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
    test_munge ();
    test_plain ();
    test_curve ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
