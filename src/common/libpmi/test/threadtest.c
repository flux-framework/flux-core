/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* threadtest.c - drive libpmi barriers concurrently from several threads
 *
 * Each rank runs NTHREADS threads for ROUNDS rounds.  In each round a
 * thread puts a key, barriers over its group, then gets the key every
 * group member put for that thread and round.  Thread i delays before
 * its barrier by an amount that depends on both i and the rank, so on
 * any given rank the threads enter in a different order and the
 * barriers complete out of order relative to the order they were
 * entered.  A thread that takes another thread's barrier_out returns
 * before every member has put its key, and a get fails or returns a
 * previous round's value.
 *
 * MODE selects the group:
 *   world  every thread barriers over all ranks.  Thread 0 uses the
 *          legacy PMI_Barrier(); the others use PMI_Barrier_group()
 *          with a per-thread tag.  This is the default.
 *   split  thread i barriers over an explicit group: ranks are cut into
 *          blocks of i+1 consecutive ranks and the group is every rank
 *          in a block of the same parity as this rank's block.  Each
 *          thread's group is thus a different subset, so a rank's
 *          threads have barriers over distinct explicit groups in
 *          flight at once.
 *   node   every thread barriers over PMI_GROUP_NODE with a per-thread
 *          tag, and gets the keys of the ranks in its clique.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#include "src/common/libutil/log.h"
#include "ccan/str/str.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"

enum mode {
    MODE_WORLD = 0,
    MODE_SPLIT,
    MODE_NODE,
};

static int rank;
static int size;
static int nthreads;
static enum mode mode;
static char kvsname[64];
static int key_len;
static int val_len;
static int *clique;
static int clique_size;

struct thread {
    pthread_t t;
    int id;
    int rounds;
    char *errmsg;
};

/* Return true if rank 'r' is in thread 'id's split-mode group, i.e. its
 * block of id+1 ranks has the same parity as this rank's block.
 */
static bool split_member (int id, int r)
{
    return (r / (id + 1)) % 2 == (rank / (id + 1)) % 2;
}

/* Build the array of ranks thread 'id' barriers over and gets keys from,
 * or NULL for all ranks.
 */
static int *group_ranks (int id, int *countp)
{
    int *ranks;
    int n = 0;

    if (mode == MODE_WORLD) {
        *countp = size;
        return NULL;
    }
    if (mode == MODE_NODE) {
        if (!(ranks = calloc (clique_size, sizeof (int))))
            return NULL;
        memcpy (ranks, clique, clique_size * sizeof (int));
        *countp = clique_size;
        return ranks;
    }
    if (!(ranks = calloc (size, sizeof (int))))
        return NULL;
    for (int r = 0; r < size; r++) {
        if (split_member (id, r))
            ranks[n++] = r;
    }
    *countp = n;
    return ranks;
}

static char *do_round (struct thread *th, int round)
{
    char *key = calloc (1, key_len);
    char *val = calloc (1, val_len);
    char *exp = calloc (1, val_len);
    char *err = NULL;
    char tag[16];
    int *ranks;
    int count = 0;
    int e;

    ranks = group_ranks (th->id, &count);
    if (!key || !val || !exp || (mode != MODE_WORLD && !ranks)) {
        err = strdup ("out of memory");
        goto done;
    }
    snprintf (key, key_len, "tt-%d-%d-%d", th->id, round, rank);
    snprintf (val, val_len, "%d.%d", round, rank);
    if ((e = PMI_KVS_Put (kvsname, key, val)) != PMI_SUCCESS
        || (e = PMI_KVS_Commit (kvsname)) != PMI_SUCCESS) {
        if (asprintf (&err, "put %s: %s", key, pmi_strerror (e)) < 0)
            err = NULL;
        goto done;
    }
    usleep (((th->id + rank) % nthreads) * 2000);
    snprintf (tag, sizeof (tag), "t%d.%d", th->id, round);
    if (mode == MODE_SPLIT)
        e = PMI_Barrier_group (ranks, count, tag);
    else if (mode == MODE_NODE)
        e = PMI_Barrier_group (PMI_GROUP_NODE, 0, tag);
    else if (th->id == 0)
        e = PMI_Barrier ();
    else
        e = PMI_Barrier_group (PMI_GROUP_WORLD, 0, tag);
    if (e != PMI_SUCCESS) {
        if (asprintf (&err, "barrier round %d: %s", round, pmi_strerror (e)) < 0)
            err = NULL;
        goto done;
    }
    for (int i = 0; i < count; i++) {
        int r = ranks ? ranks[i] : i;
        snprintf (key, key_len, "tt-%d-%d-%d", th->id, round, r);
        if ((e = PMI_KVS_Get (kvsname, key, val, val_len)) != PMI_SUCCESS) {
            if (asprintf (&err, "get %s: %s", key, pmi_strerror (e)) < 0)
                err = NULL;
            goto done;
        }
        snprintf (exp, val_len, "%d.%d", round, r);
        if (strcmp (val, exp) != 0) {
            if (asprintf (&err, "get %s: got %s want %s", key, val, exp) < 0)
                err = NULL;
            goto done;
        }
    }
done:
    free (ranks);
    free (key);
    free (val);
    free (exp);
    return err;
}

static void *thread_main (void *arg)
{
    struct thread *th = arg;

    for (int i = 0; i < th->rounds; i++) {
        if ((th->errmsg = do_round (th, i)))
            break;
    }
    return NULL;
}

int main (int argc, char *argv[])
{
    struct thread *threads;
    int rounds;
    int spawned;
    int e;

    log_init ("threadtest");
    if (argc != 3 && argc != 4)
        log_msg_exit ("Usage: threadtest NTHREADS ROUNDS [world|split|node]");
    nthreads = strtol (argv[1], NULL, 10);
    rounds = strtol (argv[2], NULL, 10);
    if (nthreads < 1 || rounds < 1)
        log_msg_exit ("NTHREADS and ROUNDS must be positive");
    if (argc == 4) {
        if (streq (argv[3], "world"))
            mode = MODE_WORLD;
        else if (streq (argv[3], "split"))
            mode = MODE_SPLIT;
        else if (streq (argv[3], "node"))
            mode = MODE_NODE;
        else
            log_msg_exit ("MODE must be world, split, or node");
    }

    if ((e = PMI_Init (&spawned)) != PMI_SUCCESS)
        log_msg_exit ("PMI_Init: %s", pmi_strerror (e));
    if ((e = PMI_Get_rank (&rank)) != PMI_SUCCESS
        || (e = PMI_Get_size (&size)) != PMI_SUCCESS)
        log_msg_exit ("PMI_Get_rank/size: %s", pmi_strerror (e));
    if ((e = PMI_KVS_Get_my_name (kvsname, sizeof (kvsname))) != PMI_SUCCESS
        || (e = PMI_KVS_Get_key_length_max (&key_len)) != PMI_SUCCESS
        || (e = PMI_KVS_Get_value_length_max (&val_len)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_*: %s", rank, pmi_strerror (e));
    if (mode == MODE_NODE) {
        if ((e = PMI_Get_clique_size (&clique_size)) != PMI_SUCCESS)
            log_msg_exit ("%d: PMI_Get_clique_size: %s", rank, pmi_strerror (e));
        if (!(clique = calloc (clique_size, sizeof (int))))
            log_msg_exit ("%d: out of memory", rank);
        if ((e = PMI_Get_clique_ranks (clique, clique_size)) != PMI_SUCCESS)
            log_msg_exit ("%d: PMI_Get_clique_ranks: %s", rank, pmi_strerror (e));
    }
    if (!(threads = calloc (nthreads, sizeof (threads[0]))))
        log_msg_exit ("%d: out of memory", rank);
    for (int i = 0; i < nthreads; i++) {
        threads[i].id = i;
        threads[i].rounds = rounds;
        e = pthread_create (&threads[i].t, NULL, thread_main, &threads[i]);
        if (e != 0)
            log_msg_exit ("%d: pthread_create: %s", rank, strerror (e));
    }
    for (int i = 0; i < nthreads; i++) {
        if ((e = pthread_join (threads[i].t, NULL)) != 0)
            log_msg_exit ("%d: pthread_join: %s", rank, strerror (e));
        if (threads[i].errmsg)
            log_msg_exit ("%d: thread %d: %s", rank, i, threads[i].errmsg);
    }
    free (threads);
    free (clique);

    if ((e = PMI_Finalize ()) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Finalize: %s", rank, pmi_strerror (e));
    if (rank == 0)
        printf ("threadtest: %d threads x %d rounds %s OK\n",
                nthreads,
                rounds,
                argc == 4 ? argv[3] : "world");
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
