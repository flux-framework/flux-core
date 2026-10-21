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
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libpmi/simple_client.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libpmi/keyval.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libflux/reactor.h"
#include "ccan/str/str.h"

#include "src/common/libtap/tap.h"

#include "server_thread.h"

int fake_publish (FILE *fp, const char *service, const char *port)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (fprintf (fp,
                 "cmd=publish_name service=%s port=%s\n",
                 service, port) < 0)
        return PMI_FAIL;
    if (!fgets (buf, sizeof (buf), fp))
        return PMI_FAIL;
    if (keyval_parse_isword (buf, "cmd", "publish_result") < 0)
        return PMI_FAIL;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    return 0;
}

int fake_unpublish (FILE *fp, const char *service)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (fprintf (fp, "cmd=unpublish_name service=%s\n", service) < 0)
        return PMI_FAIL;
    if (!fgets(buf, sizeof (buf), fp))
        return PMI_FAIL;
    if (keyval_parse_isword (buf, "cmd", "unpublish_result") < 0)
        return PMI_FAIL;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    return 0;
}

int fake_lookup (FILE *fp, const char *service)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (fprintf (fp, "cmd=lookup_name service=%s\n", service) < 0)
        return PMI_FAIL;
    if (!fgets (buf, sizeof (buf), fp))
        return PMI_FAIL;
    if (keyval_parse_isword (buf, "cmd", "lookup_result") < 0)
        return PMI_FAIL;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    return 0;
}

int fake_spawn (FILE *fp)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (fprintf (fp,
                 "mcmd=spawn\n"
                 "nprocs=1\n"
                 "execname=foo\n"
                 "totspawns=1\n"
                 "spawnssofar=1\n"
                 "argcnt=0\n"
                 "preput_num=0\n"
                 "info_num=0\n"
                 "endcmd\n") < 0)
        return PMI_FAIL;
    if (!fgets (buf, sizeof (buf), fp))
        return PMI_FAIL;
    if (keyval_parse_isword (buf, "cmd", "spawn_result") < 0)
        return PMI_FAIL;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    return 0;
}

int fake_init (FILE *fp, int version, int subversion)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (fprintf (fp,
                 "cmd=init pmi_version=%d pmi_subversion=%d\n",
                 version,
                 subversion) < 0)
        return -1;
    if (!fgets (buf, sizeof (buf), fp))
        return -1;
    if (keyval_parse_isword (buf, "cmd", "response_to_init") < 0)
        return -1;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    return 0;
}

/* Send a raw barrier_in, optionally carrying a PMI-1.2 group string. */
static int send_barrier_in (FILE *fp, const char *group)
{
    int rc;

    if (group)
        rc = fprintf (fp, "cmd=barrier_in group=%s\n", group);
    else
        rc = fprintf (fp, "cmd=barrier_in\n");
    if (rc < 0 || fflush (fp) < 0)
        return -1;
    return 0;
}

/* Read a barrier_out.  *tag is set to the echoed tag, or -1 if absent.
 * Returns the barrier rc (0 on success).
 */
static int recv_barrier_out (FILE *fp, int *tag)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (!fgets (buf, sizeof (buf), fp))
        return -1;
    if (keyval_parse_isword (buf, "cmd", "barrier_out") < 0)
        return -1;
    if (tag && keyval_parse_int (buf, "tag", tag) < 0)
        *tag = -1;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    return 0;
}

/* Drive one complete barrier (send + receive) on a single-member server. */
static int fake_barrier (FILE *fp, const char *group, int *tag)
{
    if (send_barrier_in (fp, group) < 0)
        return -1;
    return recv_barrier_out (fp, tag);
}

/* Cleanly finalize a raw client connection so the server stops its watcher. */
static int fake_finalize (FILE *fp)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (fprintf (fp, "cmd=finalize\n") < 0 || fflush (fp) < 0)
        return -1;
    if (!fgets (buf, sizeof (buf), fp))
        return -1;
    if (keyval_parse_isword (buf, "cmd", "finalize_ack") < 0)
        return -1;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    return 0;
}

/* Scripted server that answers a single client init with a rigged protocol
 * version, then answers the get_maxes the client sends on a successful init.
 * Responses are delivered in lockstep with the client's requests so that no
 * response is buffered across a stdio write/read transition (which fails on a
 * socket without an intervening fflush).
 */
struct rig_server {
    int sfd;
    int vers;
    int subvers;
};

static void *rig_server_thread (void *arg)
{
    struct rig_server *r = arg;
    char buf[SIMPLE_MAX_PROTO_LINE];
    FILE *fp;

    if (!(fp = fdopen (r->sfd, "r+")))
        BAIL_OUT ("fdopen failed");
    if (fgets (buf, sizeof (buf), fp)) { // client's cmd=init
        fprintf (fp,
                 "cmd=response_to_init rc=0 pmi_version=%d pmi_subversion=%d\n",
                 r->vers,
                 r->subvers);
        fflush (fp);
        if (fgets (buf, sizeof (buf), fp)) { // client's cmd=get_maxes (if any)
            fprintf (fp,
                     "cmd=maxes rc=0 "
                     "kvsname_max=64 keylen_max=64 vallen_max=64\n");
            fflush (fp);
        }
    }
    fclose (fp);
    return NULL;
}

/* Run the real client init against a server rigged to report protocol
 * version vers.subvers, and return the init result.
 */
static int client_init_rigged (int vers, int subvers)
{
    int fd[2];
    pthread_t t;
    struct rig_server r;
    struct pmi_simple_client *cli;
    char pmi_fd[16], pmi_rank[16], pmi_size[16];
    int rc;

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) < 0)
        BAIL_OUT ("socketpair failed");
    r.sfd = fd[1];
    r.vers = vers;
    r.subvers = subvers;
    if (pthread_create (&t, NULL, rig_server_thread, &r) != 0)
        BAIL_OUT ("pthread_create failed");
    snprintf (pmi_fd, sizeof (pmi_fd), "%d", fd[0]);
    snprintf (pmi_rank, sizeof (pmi_rank), "%d", 0);
    snprintf (pmi_size, sizeof (pmi_size), "%d", 1);
    if (!(cli = pmi_simple_client_create_fd (pmi_fd, pmi_rank, pmi_size, NULL)))
        BAIL_OUT ("pmi_simple_client_create_fd failed");
    rc = pmi_simple_client_init (cli);
    pmi_simple_client_destroy (cli); // closes fd[0]
    pthread_join (t, NULL);
    return rc;
}

int main (int argc, char *argv[])
{
    struct pmi_simple_client *cli;
    struct pmi_server_context *srv;
    int cfd[1];
    int universe_size = -1;
    char *name = NULL, *val = NULL, *val2 = NULL, *val3 = NULL;
    char *key = NULL;
    int rc;
    char pmi_fd[16];
    char pmi_rank[16];
    char pmi_size[16];
    FILE *cfp;

    plan (NO_PLAN);

    srv = pmi_server_create (cfd, 1);
    if (!(cfp = fdopen (dup (cfd[0]), "r+")))
        BAIL_OUT ("fdopen/dup failed");

    /* N.B. server allows multiple inits, so we take advantage
     * of this to beat on its version negotiation without any
     * setup/teardown of the server.
     */
    ok (fake_init (cfp, 1, 3) == -1,
        "fake init for protocol 1.3 fails");
    ok (fake_init (cfp, 2, 0) == -1,
        "fake init for protocol 2.0 fails");
    ok (fake_init (cfp, 3, 0) == -1,
        "fake init for protocol 3.0 fails");
    ok (fake_init (cfp, 1, 0) == 0,
        "fake init for protocol 1.0 succeeds");
    ok (fake_init (cfp, 1, 1) == 0,
        "fake init for protocol 1.1 succeeds");
    ok (fake_init (cfp, 1, 2) == 0,
        "fake init for protocol 1.2 succeeds");

    /* The server replies with its maximum subversion (2) even though the
     * client requires only 1.1 (RFC 13 negotiation; enables PMI 1.2 group
     * barriers for capable clients).
     */
    {
        char ibuf[SIMPLE_MAX_PROTO_LINE];
        unsigned int subvers = 0;
        int reply_rc = -1;

        if (fprintf (cfp, "cmd=init pmi_version=1 pmi_subversion=1\n") < 0
            || fflush (cfp) < 0
            || !fgets (ibuf, sizeof (ibuf), cfp))
            BAIL_OUT ("init exchange failed");
        keyval_parse_int (ibuf, "rc", &reply_rc);
        keyval_parse_uint (ibuf, "pmi_subversion", &subvers);
        ok (reply_rc == 0 && subvers == 2,
            "server bids up to pmi_subversion=2");
    }

    /* Client-side negotiation: the client offers 1.1 but MUST accept a
     * server that bids up to a higher subversion, and MUST reject an
     * incompatible major version.  (RFC 13 version negotiation.)
     */
    ok (client_init_rigged (1, 1) == PMI_SUCCESS,
        "client init succeeds when server replies pmi_subversion=1");
    ok (client_init_rigged (1, 2) == PMI_SUCCESS,
        "client init succeeds when server bids up to pmi_subversion=2");
    ok (client_init_rigged (2, 0) != PMI_SUCCESS,
        "client init fails on incompatible pmi_version=2");

    /* create/init
     */
    snprintf (pmi_fd, sizeof (pmi_fd), "%d", cfd[0]);
    snprintf (pmi_rank, sizeof (pmi_rank), "%d", 0);
    snprintf (pmi_size, sizeof (pmi_size), "%d", 1);

    ok ((cli = pmi_simple_client_create_fd (pmi_fd,
                                            pmi_rank,
                                            pmi_size,
                                            NULL)) != NULL,
        "pmi_simple_client_create OK");
    ok (cli->initialized == false,
        "cli->initialized == false");
    ok (pmi_simple_client_init (cli) == PMI_SUCCESS,
        "pmi_simple_client_init OK");
    ok (cli->spawned == false,
        "cli->spawned == failse");

    /* retrieve basic params
     */
    ok (cli->size == 1,
        "cli->size == 1");
    ok (cli->rank == 0,
        "cli->rank == 0");
    ok (pmi_simple_client_get_universe_size (cli, &universe_size) == PMI_SUCCESS
        && universe_size == cli->size,
        "pmi_simple_client_get_universe_size OK, universe_size=%d", universe_size);
    ok (cli->kvsname_max > 0,
        "cli->kvsname_max > 0");
    ok (cli->keylen_max > 0,
        "cli->keylen_max > 0");
    ok (cli->vallen_max > 0,
        "cli->vallen_max > 0");
    name = xzmalloc (cli->kvsname_max);
    ok (pmi_simple_client_kvs_get_my_name (cli, name,
                                           cli->kvsname_max) == PMI_SUCCESS
        && strlen (name) > 0,
        "pmi_simple_client_kvs_get_my_name OK");

    /* put foo=bar / commit / barier / get foo
     */
    ok (pmi_simple_client_kvs_put (cli, name, "foo", "bar") == PMI_SUCCESS,
        "pmi_simple_client_kvs_put foo=bar OK");
    ok (pmi_simple_client_barrier (cli) == PMI_SUCCESS,
        "pmi_simple_client_barrier OK");
    val = xzmalloc (cli->vallen_max);
    ok (pmi_simple_client_kvs_get (cli, name, "foo",
                                   val, cli->vallen_max) == PMI_SUCCESS
        && streq (val, "bar"),
        "pmi_simple_client_kvs_get foo OK, val=%s", val);

    /* put long=... / get long
     */
    val2 = xzmalloc (cli->vallen_max);
    memset (val2, 'x', cli->vallen_max - 1);
    ok (pmi_simple_client_kvs_put (cli, name, "long", val2) == PMI_SUCCESS,
        "pmi_simple_client_kvs_put long=xxx... OK");
    memset (val, 'y', cli->vallen_max); /* not null terminated */
    ok (pmi_simple_client_kvs_get (cli, name, "long",
                                   val, cli->vallen_max) == PMI_SUCCESS
        && strnlen (val2, cli->vallen_max) < cli->vallen_max
        && streq (val, val2),
        "pmi_simple_client_kvs_get long OK, val=xxx...");

    /* put: value too long
     */
    val3 = xzmalloc (cli->vallen_max + 1);
    memset (val3, 'y', cli->vallen_max);
    rc = pmi_simple_client_kvs_put (cli, name, "toolong", val3);
    ok (rc == PMI_ERR_INVALID_VAL_LENGTH,
        "pmi_simple_client_kvs_put val too long fails");

    /* put: key too long
     */
    key = xzmalloc (cli->keylen_max + 1);
    memset (key, 'z', cli->keylen_max);
    rc = pmi_simple_client_kvs_put (cli, name, key, "abc");
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_put key too long fails");

    /* get: key too long
     */
    rc = pmi_simple_client_kvs_get (cli, name, key, val, cli->vallen_max);
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_get key too long fails");

    /* get: no exist
     */
    rc = pmi_simple_client_kvs_get (cli, name, "noexist", val, cli->vallen_max);
    ok (rc == PMI_ERR_INVALID_KEY,
        "pmi_simple_client_kvs_get unknown key fails");

    /* PMI-1.2 group barriers: the tag is echoed in barrier_out, and the
     * legacy (no group) barrier emits no tag.  This single-member server
     * completes each barrier as soon as its one local member enters.
     */
    int tag = 0;
    ok (fake_barrier (cfp, "WORLD:5", &tag) == 0 && tag == 5,
        "barrier group=WORLD:5 -> barrier_out tag=5");
    ok (fake_barrier (cfp, "NODE:7", &tag) == 0 && tag == 7,
        "barrier group=NODE:7 -> barrier_out tag=7");
    ok (fake_barrier (cfp, "0:9", &tag) == 0 && tag == 9,
        "barrier explicit group 0:9 -> barrier_out tag=9");
    ok (fake_barrier (cfp, NULL, &tag) == 0 && tag == -1,
        "legacy barrier -> barrier_out with no tag");

    /* barrier: entry failure
     */
    pmi_set_barrier_entry_failure (srv, 1);
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with entry function failure fails");
    pmi_set_barrier_entry_failure (srv, 0);
    pmi_set_barrier_exit_failure (srv, 1);
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with exit function failure fails");
    pmi_set_barrier_exit_failure (srv, 0);
    ok (pmi_simple_client_barrier (cli) == PMI_SUCCESS,
        "pmi_simple_client_barrier OK (rigged errors cleared)");

    /* publish */
    ok (fake_publish (cfp, "foo", "bar") == PMI_FAIL,
        "publish fails (unimplemented)");

    /* unpublish */
    ok (fake_unpublish (cfp, "foo") == PMI_FAIL,
        "unpublish fails (unimplemented)");

    /* lookup */
    ok (fake_lookup (cfp, "foo") == PMI_FAIL,
        "lookup fails (unimplemented)");

    /* spawn */
    ok (fake_spawn (cfp) == PMI_FAIL,
        "spawn fails (unimplemented)");

    fclose (cfp);

    /* finalize
     */

    ok (pmi_simple_client_finalize (cli) == PMI_SUCCESS,
        "pmi_simple_client_finalize OK");

    free (name);
    free (val);
    free (val2);
    free (val3);
    free (key);
    pmi_simple_client_destroy (cli);

    pmi_server_destroy (srv);

    /* Concurrent group barriers: a 2-member server runs two WORLD barriers
     * with distinct tags at the same time.  Each barrier completes only once
     * both members enter; entries are interleaved so both are simultaneously
     * in flight, exercising the per-group barrier table.
     */
    int cfd2[2];
    struct pmi_server_context *srv2;
    FILE *f0, *f1;
    int tag0a = -1, tag0b = -1, tag1a = -1, tag1b = -1;

    srv2 = pmi_server_create (cfd2, 2);
    if (!(f0 = fdopen (cfd2[0], "r+")))
        BAIL_OUT ("fdopen failed");
    if (!(f1 = fdopen (cfd2[1], "r+")))
        BAIL_OUT ("fdopen failed");
    ok (send_barrier_in (f0, "WORLD:5") == 0
        && send_barrier_in (f0, "WORLD:6") == 0
        && send_barrier_in (f1, "WORLD:5") == 0
        && send_barrier_in (f1, "WORLD:6") == 0,
        "two concurrent WORLD barriers: all members entered");
    ok (recv_barrier_out (f0, &tag0a) == 0
        && recv_barrier_out (f0, &tag0b) == 0
        && recv_barrier_out (f1, &tag1a) == 0
        && recv_barrier_out (f1, &tag1b) == 0,
        "two concurrent WORLD barriers: all members responded");
    ok (tag0a == 5 && tag1a == 5 && tag0b == 6 && tag1b == 6,
        "concurrent barriers completed independently with tags 5 and 6");

    /* NODE expects every local rank, and is keyed apart from WORLD even
     * with the same tag.  Rank 0 enters NODE:5 and rank 1 enters WORLD:5;
     * neither completes.  Rank 1 then enters NODE:5, completing only it.
     */
    int ntag = -1;
    ok (send_barrier_in (f0, "NODE:5") == 0
        && send_barrier_in (f1, "WORLD:5") == 0
        && send_barrier_in (f1, "NODE:5") == 0
        && recv_barrier_out (f0, &ntag) == 0 && ntag == 5
        && recv_barrier_out (f1, &ntag) == 0 && ntag == 5,
        "NODE barrier completes when both local ranks enter it");
    ok (send_barrier_in (f0, "WORLD:5") == 0
        && recv_barrier_out (f0, &ntag) == 0 && ntag == 5
        && recv_barrier_out (f1, &ntag) == 0 && ntag == 5,
        "WORLD barrier with the same tag completed separately");

    ok (fake_finalize (f0) == 0 && fake_finalize (f1) == 0,
        "raw clients finalized");
    fclose (f0);
    fclose (f1);
    pmi_server_destroy (srv2);

    /* Invalid barrier_in requests are protocol errors: the test server
     * closes the offending connection, so the client reads EOF instead of
     * barrier_out.  Each case gets a fresh 2-member server since the
     * first error on a connection ends it.
     */
    const char *badgroup[] = {
        "WORLD",            // missing :tag
        "WORLD:",           // empty tag
        "WORLD:x",          // non-numeric tag
        "0,1:5x",           // trailing garbage after tag
        "zz:5",             // undecodable rank list
        "1:5",              // rank 0 enters a group it is not in
    };
    for (int i = 0; i < sizeof (badgroup) / sizeof (badgroup[0]); i++) {
        int cfd3[2];
        struct pmi_server_context *srv3;
        FILE *f;

        srv3 = pmi_server_create (cfd3, 2);
        if (!(f0 = fdopen (cfd3[0], "r+")))
            BAIL_OUT ("fdopen failed");
        if (!(f1 = fdopen (cfd3[1], "r+")))
            BAIL_OUT ("fdopen failed");
        f = f0;
        ok (send_barrier_in (f, badgroup[i]) == 0
            && recv_barrier_out (f, &ntag) < 0
            && feof (f),
            "barrier_in group=%s is a protocol error", badgroup[i]);
        ok (fake_finalize (f1) == 0, "rank 1 unaffected and finalized");
        fclose (f0);
        fclose (f1);
        pmi_server_destroy (srv3);
    }

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
