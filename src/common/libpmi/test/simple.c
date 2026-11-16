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
#include <poll.h>
#include <stdbool.h>
#include <string.h>
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

/* Threaded client test helper: enter group barrier 'tag' on 'cli' and
 * record the result.
 */
struct barrier_arg {
    struct pmi_simple_client *cli;
    const char *group;
    int tag;
    int result;
};

static void *barrier_thread (void *arg)
{
    struct barrier_arg *a = arg;

    /* group == NULL selects the legacy whole-job barrier */
    if (a->group)
        a->result = pmi_simple_client_barrier_group (a->cli, a->group, a->tag);
    else
        a->result = pmi_simple_client_barrier (a->cli);
    return NULL;
}

/* Spin until a client has 'n' barriers in flight, i.e. the server has seen
 * n barrier_in lines from it.  Used to order the test deterministically.
 */
static void wait_barrier_in (FILE *peer, const char *group)
{
    /* The peer (rank 1) enters the same barrier so it can complete once
     * rank 0's thread has entered.  Entering here blocks until the server
     * responds, i.e. until both members are in, which is what we want.
     */
    int tag;
    if (fake_barrier (peer, group, &tag) < 0)
        BAIL_OUT ("peer barrier %s failed", group ? group : "(legacy)");
}

/* Create a client on a socketpair whose other end, returned in *sfd, the
 * test drives as a scripted server.  Responses may be written ahead of
 * the requests they answer, since the client only reads them.
 */
static struct pmi_simple_client *scripted_client_create (int *sfd)
{
    int fd[2];
    char pmi_fd[16];
    struct pmi_simple_client *cli;

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) < 0)
        BAIL_OUT ("socketpair failed");
    snprintf (pmi_fd, sizeof (pmi_fd), "%d", fd[0]);
    if (!(cli = pmi_simple_client_create_fd (pmi_fd, "0", "2", NULL)))
        BAIL_OUT ("pmi_simple_client_create_fd failed");
    *sfd = fd[1];
    return cli;
}

static void scripted_send (int sfd, const char *s)
{
    if (write (sfd, s, strlen (s)) != strlen (s))
        BAIL_OUT ("scripted server write failed");
}

/* Read one request line from the client, unbuffered so that a later
 * check for further requests sees everything not yet read here.
 */
static void scripted_recv (int sfd, char *buf, int len)
{
    int n = 0;

    while (n < len - 1) {
        if (read (sfd, &buf[n], 1) != 1)
            BAIL_OUT ("scripted server read failed");
        if (buf[n++] == '\n')
            break;
    }
    buf[n] = '\0';
}

/* Return true if the client has sent nothing the server has not read.
 */
static bool scripted_idle (int sfd)
{
    struct pollfd pfd = { .fd = sfd, .events = POLLIN };

    return poll (&pfd, 1, 0) == 0;
}

static struct pmi_simple_client *scripted_client_init (int *sfd)
{
    struct pmi_simple_client *cli = scripted_client_create (sfd);
    char buf[SIMPLE_MAX_PROTO_LINE];

    scripted_send (*sfd,
                   "cmd=response_to_init rc=0 pmi_version=1 pmi_subversion=2\n"
                   "cmd=maxes rc=0 kvsname_max=64 keylen_max=64 vallen_max=64\n");
    if (pmi_simple_client_init (cli) != PMI_SUCCESS)
        BAIL_OUT ("scripted client init failed");
    scripted_recv (*sfd, buf, sizeof (buf)); // cmd=init
    scripted_recv (*sfd, buf, sizeof (buf)); // cmd=get_maxes
    return cli;
}

/* A failed init leaves the client as it was.
 */
static void test_init_failure (void)
{
    struct pmi_simple_client *cli;
    int sfd;

    cli = scripted_client_create (&sfd);
    scripted_send (sfd,
                   "cmd=response_to_init rc=0 pmi_version=1 pmi_subversion=2\n"
                   "cmd=maxes rc=0 kvsname_max=64\n");
    ok (pmi_simple_client_init (cli) == PMI_FAIL,
        "init fails when maxes response is incomplete");
    ok (cli->initialized == 0
            && cli->version == 0
            && cli->subversion == 0
            && cli->kvsname_max == 0,
        "failed init leaves version and maxes unset");
    pmi_simple_client_destroy (cli);
    close (sfd);
}

/* A barrier that loses its connection fails, even after parking a
 * successful reply for another tag, and fails the client.
 */
static void test_barrier_lost_connection (void)
{
    struct pmi_simple_client *cli;
    int sfd;

    cli = scripted_client_init (&sfd);
    scripted_send (sfd, "cmd=barrier_out tag=9 rc=0\n");
    shutdown (sfd, SHUT_WR);
    ok (pmi_simple_client_barrier_group (cli, "WORLD", 5) == PMI_FAIL,
        "barrier fails on EOF after parking another tag's rc=0 reply");
    ok (pmi_simple_client_barrier_group (cli, "WORLD", 9) == PMI_FAIL,
        "later barrier fails without claiming the stale parked reply");
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "later whole-job barrier fails too");
    pmi_simple_client_destroy (cli);
    close (sfd);
}

/* A barrier whose tag is already in flight on the client is refused
 * without being sent, since its reply could not be told apart.
 */
static void test_barrier_duplicate_tag (void)
{
    struct pmi_simple_client *cli;
    int sfd;
    char buf[SIMPLE_MAX_PROTO_LINE];
    pthread_t ta, tc;
    struct barrier_arg a = { .group = "WORLD", .tag = 5, .result = -1 };
    struct barrier_arg c = { .group = NULL, .result = -1 };

    cli = scripted_client_init (&sfd);
    a.cli = c.cli = cli;
    if (pthread_create (&ta, NULL, barrier_thread, &a) != 0)
        BAIL_OUT ("pthread_create failed");
    scripted_recv (sfd, buf, sizeof (buf));
    ok (streq (buf, "cmd=barrier_in group=WORLD:5\n"),
        "duplicate: group barrier tag 5 sent");
    if (pthread_create (&tc, NULL, barrier_thread, &c) != 0)
        BAIL_OUT ("pthread_create failed");
    scripted_recv (sfd, buf, sizeof (buf));
    ok (streq (buf, "cmd=barrier_in\n"), "duplicate: whole-job barrier sent");

    ok (pmi_simple_client_barrier_group (cli, "NODE", 5) == PMI_FAIL,
        "duplicate: second barrier with tag 5 is refused");
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "duplicate: second whole-job barrier is refused");
    ok (scripted_idle (sfd), "duplicate: refused barriers were not sent");

    scripted_send (sfd, "cmd=barrier_out rc=0\ncmd=barrier_out tag=5 rc=0\n");
    if (pthread_join (ta, NULL) != 0 || pthread_join (tc, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (a.result == PMI_SUCCESS && c.result == PMI_SUCCESS,
        "duplicate: barriers in flight still complete");
    pmi_simple_client_destroy (cli);
    close (sfd);
}

/* Send a raw kvs get and read get_result, returning the value in 'val'. */
static int fake_get (FILE *fp, const char *key, char *val, int len)
{
    char buf[SIMPLE_MAX_PROTO_LINE];
    int rc;

    if (fprintf (fp, "cmd=get kvsname=x key=%s\n", key) < 0 || fflush (fp) < 0)
        return -1;
    if (!fgets (buf, sizeof (buf), fp))
        return -1;
    if (keyval_parse_isword (buf, "cmd", "get_result") < 0)
        return -1;
    if (keyval_parse_int (buf, "rc", &rc) == 0 && rc != 0)
        return rc;
    if (val && keyval_parse_string (buf, "value", val, len) < 0)
        return -1;
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

    /* String tag hashing matches MPICH (FNV-1a, abs of the int cast).
     * "a" hashes to 0xe40c292c, whose int cast is negative.
     */
    ok (pmi_simple_client_barrier_tag (NULL) == 0,
        "pmi_simple_client_barrier_tag NULL -> 0");
    ok (pmi_simple_client_barrier_tag ("hello") == 1335831723,
        "pmi_simple_client_barrier_tag hello -> 1335831723");
    ok (pmi_simple_client_barrier_tag ("a") == 468965076,
        "pmi_simple_client_barrier_tag a -> 468965076 (negated)");

    /* Client-side PMI-1.2 group barrier against the real server.
     */
    ok (cli->version == 1 && cli->subversion == 2,
        "client recorded negotiated version 1.2");
    ok (pmi_simple_client_barrier_group (cli, "WORLD", 11) == PMI_SUCCESS,
        "pmi_simple_client_barrier_group WORLD tag=11 OK");
    ok (pmi_simple_client_barrier_group (cli, "NODE", 0) == PMI_SUCCESS,
        "pmi_simple_client_barrier_group NODE tag=0 OK");
    ok (pmi_simple_client_barrier_group (cli, "0", 3) == PMI_SUCCESS,
        "pmi_simple_client_barrier_group explicit rank 0 tag=3 OK");
    ok (pmi_simple_client_barrier_group (cli, NULL, 1) == PMI_ERR_INVALID_ARG,
        "pmi_simple_client_barrier_group group=NULL fails");
    rc = pmi_simple_client_barrier_group (cli, "WORLD", -1);
    ok (rc == PMI_ERR_INVALID_ARG, "pmi_simple_client_barrier_group tag=-1 fails");
    cli->subversion = 1;
    ok (pmi_simple_client_barrier_group (cli, "WORLD", 1) == PMI_FAIL,
        "pmi_simple_client_barrier_group fails if server subversion < 2");
    cli->subversion = 2;

    /* Threaded client: two threads on one client each enter a different
     * tagged barrier, interleaved with a kvs get from a third thread, on
     * a 2-member server.  Rank 1 is a raw connection the test drives to
     * complete the barriers in the reverse of the order rank 0's threads
     * entered them.  This exercises the client's tag matching and the
     * parking of barrier_out lines read on behalf of other threads.
     */
    int cfd3[2];
    struct pmi_server_context *srv3;
    struct pmi_simple_client *cli3;
    FILE *peer;
    pthread_t ta, tb;
    struct barrier_arg a = { .group = "WORLD", .tag = 100, .result = -1 };
    struct barrier_arg b = { .group = "WORLD", .tag = 200, .result = -1 };
    char getbuf[64] = "";

    srv3 = pmi_server_create (cfd3, 2);
    snprintf (pmi_fd, sizeof (pmi_fd), "%d", cfd3[0]);
    if (!(cli3 = pmi_simple_client_create_fd (pmi_fd, "0", "2", NULL)))
        BAIL_OUT ("pmi_simple_client_create_fd failed");
    ok (pmi_simple_client_init (cli3) == PMI_SUCCESS, "threaded: client init OK");
    if (!(peer = fdopen (cfd3[1], "r+")))
        BAIL_OUT ("fdopen failed");
    a.cli = b.cli = cli3;

    /* Thread A enters tag 100; thread B enters tag 200.  Neither can
     * complete until rank 1 enters, so both are blocked in the client
     * with the lock released.
     */
    if (pthread_create (&ta, NULL, barrier_thread, &a) != 0)
        BAIL_OUT ("pthread_create failed");
    if (pthread_create (&tb, NULL, barrier_thread, &b) != 0)
        BAIL_OUT ("pthread_create failed");

    /* With two barriers pending on rank 0's connection, a kvs get from
     * the main thread must still be serviced (it holds the lock for its
     * round trip; the barrier threads must not have it).
     */
    ok (pmi_simple_client_kvs_get (cli3,
                                   "bleepgorp",
                                   "test_key",
                                   getbuf,
                                   sizeof (getbuf)) == PMI_SUCCESS
            && streq (getbuf, "test_val"),
        "threaded: kvs get serviced while two barriers are pending");

    /* Complete B's barrier (tag 200) first, then A's (tag 100).  Whichever
     * thread reads tag=200 off the wire must hand it to B even if it was
     * A's turn to read.
     */
    wait_barrier_in (peer, "WORLD:200");
    if (pthread_join (tb, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (b.result == PMI_SUCCESS,
        "threaded: later-entered barrier (tag 200) completed first");
    ok (a.result == -1, "threaded: earlier-entered barrier (tag 100) still pending");
    wait_barrier_in (peer, "WORLD:100");
    if (pthread_join (ta, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (a.result == PMI_SUCCESS,
        "threaded: earlier-entered barrier (tag 100) completed second");

    /* Both threads now start again with the same tags; this time complete
     * them in order, to check nothing stale is left in the parking list.
     */
    a.result = b.result = -1;
    if (pthread_create (&ta, NULL, barrier_thread, &a) != 0)
        BAIL_OUT ("pthread_create failed");
    if (pthread_create (&tb, NULL, barrier_thread, &b) != 0)
        BAIL_OUT ("pthread_create failed");
    wait_barrier_in (peer, "WORLD:100");
    wait_barrier_in (peer, "WORLD:200");
    if (pthread_join (ta, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    if (pthread_join (tb, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (a.result == PMI_SUCCESS && b.result == PMI_SUCCESS,
        "threaded: second round of two barriers completed");

    /* A legacy pmi_simple_client_barrier() is sent untagged and holds
     * the lock for its round trip, as MPICH does.  A group barrier sent
     * earlier by thread A is still in flight when thread C sends the
     * legacy barrier and becomes the reader.  Complete A's barrier
     * first: C reads the tagged reply, must park it for A rather than
     * take it as its own, and A must then wake and finish.  Then
     * complete C's.
     */
    struct barrier_arg c = { .cli = cli3, .group = NULL, .result = -1 };
    int nseen;
    a.result = -1;
    nseen = pmi_server_barrier_in_count (srv3, 0);
    if (pthread_create (&ta, NULL, barrier_thread, &a) != 0)
        BAIL_OUT ("pthread_create failed");
    pmi_server_wait_barrier_in (srv3, 0, "WORLD:100", nseen);
    nseen = pmi_server_barrier_in_count (srv3, 0);
    if (pthread_create (&tb, NULL, barrier_thread, &c) != 0)
        BAIL_OUT ("pthread_create failed");
    pmi_server_wait_barrier_in (srv3, 0, NULL, nseen);
    wait_barrier_in (peer, "WORLD:100");
    if (pthread_join (ta, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (a.result == PMI_SUCCESS && c.result == -1,
        "threaded: group barrier completed while legacy barrier pending");
    wait_barrier_in (peer, NULL);
    if (pthread_join (tb, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (c.result == PMI_SUCCESS, "threaded: untagged legacy barrier then completed");

    /* Hydra tags its reply to an untagged barrier_in.  The client must
     * take that reply as its own rather than parking it as another
     * thread's, which would hang the barrier.  Again with a group
     * barrier in flight, whose tag the rigged reply must not match.
     */
    pmi_set_barrier_out_tag (srv3, 42);
    a.result = c.result = -1;
    nseen = pmi_server_barrier_in_count (srv3, 0);
    if (pthread_create (&ta, NULL, barrier_thread, &a) != 0)
        BAIL_OUT ("pthread_create failed");
    pmi_server_wait_barrier_in (srv3, 0, "WORLD:100", nseen);
    nseen = pmi_server_barrier_in_count (srv3, 0);
    if (pthread_create (&tb, NULL, barrier_thread, &c) != 0)
        BAIL_OUT ("pthread_create failed");
    pmi_server_wait_barrier_in (srv3, 0, NULL, nseen);
    wait_barrier_in (peer, NULL);
    if (pthread_join (tb, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (c.result == PMI_SUCCESS && a.result == -1,
        "threaded: legacy barrier accepts a tagged barrier_out (Hydra)");
    pmi_set_barrier_out_tag (srv3, 0);
    wait_barrier_in (peer, "WORLD:100");
    if (pthread_join (ta, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (a.result == PMI_SUCCESS, "threaded: group barrier then completed");

    /* An explicit rank list need not ascend (RFC 22): MPICH lists the
     * node roots in communicator rank order, so a communicator built from
     * a reversed group sends e.g. "1,0" (flux-core issue #7888).  The
     * client passes the list through unchanged, and the barrier completes
     * only once both members enter.
     */
    struct barrier_arg d = { .cli = cli3, .group = "1,0", .tag = 8, .result = -1 };
    if (pthread_create (&ta, NULL, barrier_thread, &d) != 0)
        BAIL_OUT ("pthread_create failed");
    wait_barrier_in (peer, "1,0:8");
    if (pthread_join (ta, NULL) != 0)
        BAIL_OUT ("pthread_join failed");
    ok (d.result == PMI_SUCCESS,
        "threaded: explicit group 1,0 with descending ranks completed");

    ok (pmi_simple_client_finalize (cli3) == PMI_SUCCESS,
        "threaded: client finalize OK");
    ok (fake_finalize (peer) == 0, "threaded: peer finalized");
    pmi_simple_client_destroy (cli3);
    fclose (peer);
    pmi_server_destroy (srv3);

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

    /* Out-of-order completion on a single connection: rank 0 has two barriers
     * outstanding (WORLD:10 then WORLD:11), but WORLD:11 is driven to
     * completion first.  rank 0 must therefore read tag=11 before tag=10,
     * which only works because barrier_out is matched by tag, not by order.
     */
    int t = -1;
    ok (send_barrier_in (f0, "WORLD:10") == 0    // A: rank0 in
        && send_barrier_in (f0, "WORLD:11") == 0 // B: rank0 in
        && send_barrier_in (f1, "WORLD:11") == 0 // B: rank1 in -> B completes
        && recv_barrier_out (f0, &t) == 0 && t == 11
        && recv_barrier_out (f1, &t) == 0 && t == 11,
        "later-requested barrier (tag 11) completes first");
    ok (send_barrier_in (f1, "WORLD:10") == 0    // A: rank1 in -> A completes
        && recv_barrier_out (f0, &t) == 0 && t == 10
        && recv_barrier_out (f1, &t) == 0 && t == 10,
        "earlier-requested barrier (tag 10) completes second, matched by tag");

    /* Interleave a non-barrier op on a connection with a barrier pending:
     * rank 0 enters a barrier (incomplete, needs rank 1), then does a kvs get.
     * The get is answered while the barrier is still outstanding, proving the
     * engine is not lock-step per connection.
     */
    char getval[SIMPLE_MAX_PROTO_LINE] = "";
    ok (send_barrier_in (f0, "WORLD:12") == 0    // pending, needs rank 1
        && fake_get (f0, "test_key", getval, sizeof (getval)) == 0
        && streq (getval, "test_val"),
        "kvs get answered while a barrier is pending on the same connection");
    ok (send_barrier_in (f1, "WORLD:12") == 0    // completes the barrier
        && recv_barrier_out (f0, &t) == 0 && t == 12
        && recv_barrier_out (f1, &t) == 0 && t == 12,
        "the pending barrier then completes (tag 12)");

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

    /* A long explicit rank list.  The group string grows with the job
     * (MPICH lists one rank per node), so it is bounded by the request
     * line rather than by the KVS limits that bound every other field.
     * The list here is longer than SIMPLE_MAX_PROTO_LINE; both local
     * members enter and must get barrier_out with the tag, rather than
     * having the group silently dropped and the legacy barrier run.
     */
    char *longgroup;
    size_t longlen = 0;
    int cfd4[2];
    struct pmi_server_context *srv4;
    int t0 = -1, t1 = -1;

    for (int i = 0; i < 400; i++)
        longlen += snprintf (NULL, 0, "%d,", i);
    longgroup = xzmalloc (longlen + 8);
    for (int i = 0, n = 0; i < 400; i++)
        n += sprintf (longgroup + n, "%s%d", i > 0 ? "," : "", i);
    strcat (longgroup, ":7");
    ok (strlen (longgroup) > SIMPLE_MAX_PROTO_LINE,
        "long group string of %zu bytes exceeds SIMPLE_MAX_PROTO_LINE",
        strlen (longgroup));
    srv4 = pmi_server_create (cfd4, 2);
    if (!(f0 = fdopen (cfd4[0], "r+")))
        BAIL_OUT ("fdopen failed");
    if (!(f1 = fdopen (cfd4[1], "r+")))
        BAIL_OUT ("fdopen failed");
    ok (send_barrier_in (f0, longgroup) == 0
        && send_barrier_in (f1, longgroup) == 0
        && recv_barrier_out (f0, &t0) == 0
        && recv_barrier_out (f1, &t1) == 0
        && t0 == 7
        && t1 == 7,
        "barrier_in with a long group string completes with its tag");
    ok (fake_finalize (f0) == 0 && fake_finalize (f1) == 0,
        "long group: both members finalized");
    fclose (f0);
    fclose (f1);
    pmi_server_destroy (srv4);
    free (longgroup);

    test_init_failure ();
    test_barrier_lost_connection ();
    test_barrier_duplicate_tag ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
