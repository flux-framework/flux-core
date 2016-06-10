#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <getopt.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libpmi-client/pmi-client.h"

#define OPTIONS "nN:l:"
static const struct option longopts[] = {
    {"n-squared",    no_argument,        0, 'n'},
    {"key-count",    required_argument,  0, 'N'},
    {"library",      required_argument,  0, 'l'},
    {0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
    struct timespec t;
    int rank, size;
    int e, spawned, initialized, kvsname_len, key_len, val_len;
    char *kvsname, *key, *val, *val2;
    bool nsquared = false;
    int ch;
    int i, j, keycount = 1;
    char *library = NULL;
    pmi_t *pmi;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'n':   /* --n-squared */
                nsquared = true;
                break;
            case 'N':   /* --key-count N */
                keycount = strtoul (optarg, NULL, 10);
                break;
            case 'l':   /* --library */
                library = optarg;
                break;
        }
    }

    /* Initial handshake with PMI obtains
     *    rank, size, and some string max lengths
     */
    if (library)
        pmi = pmi_create_dlopen (library);
    else
        pmi = pmi_create_guess ();
    if (!pmi)
        err_exit ("pmi_create");
    e = pmi_init (pmi, &spawned);
    if (e != PMI_SUCCESS)
        log_msg_exit ("pmi_init: %s", pmi_strerror (e));
    e = pmi_initialized (pmi, &initialized);
    if (e != PMI_SUCCESS)
        log_msg_exit ("pmi_initialized: %s", pmi_strerror (e));
    if (initialized == 0)
        log_msg_exit ("pmi_initialized says nope!");
    e = pmi_get_rank (pmi, &rank);
    if (e != PMI_SUCCESS)
        log_msg_exit ("pmi_get_rank: %s", pmi_strerror (e));
    e = pmi_get_size (pmi, &size);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_get_size: %s",
                rank, pmi_strerror (e));
    e = pmi_kvs_get_name_length_max (pmi, &kvsname_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_kvs_get_name_length_max: %s",
                rank, pmi_strerror (e));
    e = pmi_kvs_get_key_length_max (pmi, &key_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_kvs_get_key_length_max: %s",
                rank, pmi_strerror (e));
    e = pmi_kvs_get_value_length_max (pmi, &val_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_kvs_get_value_length_max: %s",
                rank, pmi_strerror (e));

    kvsname = xzmalloc (kvsname_len);
    key = xzmalloc (key_len);
    val = xzmalloc (val_len);
    val2 = xzmalloc (val_len);

    e = pmi_kvs_get_my_name (pmi, kvsname, kvsname_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_kvs_get_my_name: %s", rank, pmi_strerror (e));

    /* Put phase
     * (keycount * PUT) + COMMIT + BARRIER
     */
    monotime (&t);
    for (i = 0; i < keycount; i++) {
        snprintf (key, key_len, "kvstest-%d-%d", rank, i);
        snprintf (val, val_len, "sandwich.%d.%d", rank, i);
        e = pmi_kvs_put (pmi, kvsname, key, val);
        if (e != PMI_SUCCESS)
            log_msg_exit ("%d: pmi_kvs_put: %s", rank, pmi_strerror (e));
    }
    e = pmi_kvs_commit (pmi, kvsname);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_kvs_commit: %s", rank, pmi_strerror (e));
    e = pmi_barrier (pmi);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_barrier: %s", rank, pmi_strerror (e));
    if (rank == 0)
        printf ("%d: put phase: %.3f sec\n", rank, monotime_since (t));

    /* Get phase
     * no options:    (keycount * GET) + BARRIER
     * --n-squared:   (keycount * GET * size) + BARRIER
     */
    monotime (&t);
    for (i = 0; i < keycount; i++) {
        if (nsquared) {
            for (j = 0; j < size; j++) {
                snprintf (key, key_len, "kvstest-%d-%d", j, i);
                e = pmi_kvs_get (pmi, kvsname, key, val, val_len);
                if (e != PMI_SUCCESS)
                    log_msg_exit ("%d: pmi_kvs_get: %s", rank, pmi_strerror (e));
                snprintf (val2, val_len, "sandwich.%d.%d", j, i);
                if (strcmp (val, val2) != 0)
                    log_msg_exit ("%d: pmi_kvs_get: exp %s got %s\n",
                             rank, val2, val);
            }
        } else {
            snprintf (key, key_len, "kvstest-%d-%d",
                      rank > 0 ? rank - 1 : size - 1, i);
            e = pmi_kvs_get (pmi, kvsname, key, val, val_len);
            if (e != PMI_SUCCESS)
                log_msg_exit ("%d: pmi_kvs_get: %s", rank, pmi_strerror (e));
            snprintf (val2, val_len, "sandwich.%d.%d",
                      rank > 0 ? rank - 1 : size - 1, i);
            if (strcmp (val, val2) != 0)
                log_msg_exit ("%d: pmi_kvs_get: exp %s got %s\n", rank, val2, val);
        }
    }
    e = pmi_barrier (pmi);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_barrier: %s", rank, pmi_strerror (e));
    if (rank == 0)
        printf ("%d: get phase: %.3f sec\n", rank, monotime_since (t));

    e = pmi_finalize (pmi);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_finalize: %s", rank, pmi_strerror (e));

    free (val);
    free (val2);
    free (key);
    free (kvsname);
    pmi_destroy (pmi);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
