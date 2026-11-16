/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* grouptest.c - check PMI-1.2 group barrier KVS visibility via libpmi
 *
 * Every rank puts a key.  For each GROUP in turn, ranks in GROUP execute
 * a group barrier and verify they can get every member's key.  Then all
 * ranks execute a whole-job barrier and verify they can get every key.
 * GROUP is an idset of ranks, or "node" for PMI_GROUP_NODE, in which
 * case every rank barriers with its clique (the ranks on its node).
 * With -r, explicit groups are passed to PMI_Barrier_group() in
 * descending rank order, as MPICH may do (flux-core issue #7888).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <flux/idset.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "ccan/str/str.h"

static int rank;
static int size;
static char *kvsname;
static int key_len;
static int val_len;

static void put_mine (void)
{
    char *key = xzmalloc (key_len);
    char *val = xzmalloc (val_len);
    int e;

    snprintf (key, key_len, "grouptest-%d", rank);
    snprintf (val, val_len, "value.%d", rank);
    if ((e = PMI_KVS_Put (kvsname, key, val)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Put: %s", rank, pmi_strerror (e));
    if ((e = PMI_KVS_Commit (kvsname)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Commit: %s", rank, pmi_strerror (e));
    free (key);
    free (val);
}

/* Verify this rank can get the key of every rank in 'ids'.
 */
static void check_visible (const struct idset *ids, const char *what)
{
    char *key = xzmalloc (key_len);
    char *val = xzmalloc (val_len);
    char *val2 = xzmalloc (val_len);
    unsigned int r;
    int e;

    r = idset_first (ids);
    while (r != IDSET_INVALID_ID) {
        snprintf (key, key_len, "grouptest-%u", r);
        snprintf (val2, val_len, "value.%u", r);
        if ((e = PMI_KVS_Get (kvsname, key, val, val_len)) != PMI_SUCCESS)
            log_msg_exit ("%d: PMI_KVS_Get %s after %s: %s",
                          rank,
                          key,
                          what,
                          pmi_strerror (e));
        if (!streq (val, val2))
            log_msg_exit ("%d: PMI_KVS_Get %s after %s: got %s expected %s",
                          rank,
                          key,
                          what,
                          val,
                          val2);
        r = idset_next (ids, r);
    }
    free (key);
    free (val);
    free (val2);
}

/* Barrier over 'group' (an idset, or NULL for PMI_GROUP_NODE) if this
 * rank is a member, then verify every member's key is visible.
 */
static void group_phase (const struct idset *group, const char *what, bool reverse)
{
    int *ranks;
    int count;
    unsigned int r;
    int i;
    int e;

    if (!group) {
        e = PMI_Barrier_group (PMI_GROUP_NODE, 0, "grouptest");
        if (e != PMI_SUCCESS)
            log_msg_exit ("%d: PMI_Barrier_group NODE: %s", rank, pmi_strerror (e));
        return;
    }
    if (!idset_test (group, rank))
        return;
    count = idset_count (group);
    ranks = xzmalloc (count * sizeof (int));
    i = reverse ? count - 1 : 0;
    r = idset_first (group);
    while (r != IDSET_INVALID_ID) {
        ranks[i] = r;
        i += reverse ? -1 : 1;
        r = idset_next (group, r);
    }
    if ((e = PMI_Barrier_group (ranks, count, "grouptest")) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Barrier_group %s: %s", rank, what, pmi_strerror (e));
    free (ranks);
    check_visible (group, what);
}

/* Return this rank's clique as an idset.
 */
static struct idset *clique_idset (void)
{
    struct idset *ids;
    int *ranks;
    int count;
    int e;

    if ((e = PMI_Get_clique_size (&count)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Get_clique_size: %s", rank, pmi_strerror (e));
    ranks = xzmalloc (count * sizeof (int));
    if ((e = PMI_Get_clique_ranks (ranks, count)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Get_clique_ranks: %s", rank, pmi_strerror (e));
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        log_msg_exit ("%d: error creating idset", rank);
    for (int i = 0; i < count; i++) {
        if (idset_set (ids, ranks[i]) < 0)
            log_msg_exit ("%d: error creating idset", rank);
    }
    free (ranks);
    return ids;
}

int main (int argc, char *argv[])
{
    struct idset *all;
    struct idset *clique = NULL;
    bool reverse = false;
    int first = 1;
    int spawned;
    int kvsname_len;
    int e;

    log_init ("grouptest");
    if (argc > 1 && streq (argv[1], "-r")) {
        reverse = true;
        first = 2;
    }
    if (argc <= first)
        log_msg_exit ("Usage: grouptest [-r] GROUP-IDSET|node [GROUP...]");

    if ((e = PMI_Init (&spawned)) != PMI_SUCCESS)
        log_msg_exit ("PMI_Init: %s", pmi_strerror (e));
    if ((e = PMI_Get_rank (&rank)) != PMI_SUCCESS)
        log_msg_exit ("PMI_Get_rank: %s", pmi_strerror (e));
    if ((e = PMI_Get_size (&size)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Get_size: %s", rank, pmi_strerror (e));
    if ((e = PMI_KVS_Get_name_length_max (&kvsname_len)) != PMI_SUCCESS
        || (e = PMI_KVS_Get_key_length_max (&key_len)) != PMI_SUCCESS
        || (e = PMI_KVS_Get_value_length_max (&val_len)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_*_length_max: %s", rank, pmi_strerror (e));
    kvsname = xzmalloc (kvsname_len);
    if ((e = PMI_KVS_Get_my_name (kvsname, kvsname_len)) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_my_name: %s", rank, pmi_strerror (e));

    if (!(all = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_range_set (all, 0, size - 1) < 0)
        log_msg_exit ("%d: error creating idset", rank);

    /* Everyone puts; then for each group, only members barrier and check
     * group keys.  The put is delayed by rank so that a barrier that
     * completes before all members have entered lets an early member see
     * a missing key.
     */
    usleep (rank * 20000);
    put_mine ();
    for (int i = first; i < argc; i++) {
        struct idset *group;

        if (streq (argv[i], "node")) {
            if (!clique)
                clique = clique_idset ();
            group_phase (NULL, "node barrier", false);
            check_visible (clique, "node barrier");
        }
        else {
            if (!(group = idset_decode (argv[i])))
                log_msg_exit ("could not decode group '%s'", argv[i]);
            group_phase (group, argv[i], reverse);
            idset_destroy (group);
        }
    }

    /* Then everyone barriers and checks all keys.
     */
    if ((e = PMI_Barrier ()) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Barrier: %s", rank, pmi_strerror (e));
    check_visible (all, "world barrier");

    if ((e = PMI_Finalize ()) != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Finalize: %s", rank, pmi_strerror (e));
    if (rank == 0) {
        printf ("grouptest:");
        for (int i = first; i < argc; i++)
            printf (" group=%s", argv[i]);
        printf (" size=%d%s OK\n", size, reverse ? " reversed" : "");
    }

    idset_destroy (clique);
    idset_destroy (all);
    free (kvsname);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
