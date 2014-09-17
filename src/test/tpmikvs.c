#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <getopt.h>

#include "pmi.h"

typedef struct {
    int errnum;
    const char *errstr;
} etab_t;

etab_t pmi_errors[] = {
    { PMI_SUCCESS,              "operation completed successfully" },
    { PMI_FAIL,                 "operation failed" },
    { PMI_ERR_NOMEM,            "input buffer not large enough" },
    { PMI_ERR_INIT,             "PMI not initialized" },
    { PMI_ERR_INVALID_ARG,      "invalid argument" },
    { PMI_ERR_INVALID_KEY,      "invalid key argument" },
    { PMI_ERR_INVALID_KEY_LENGTH,"invalid key length argument" },
    { PMI_ERR_INVALID_VAL,      "invalid val argument" },
    { PMI_ERR_INVALID_VAL_LENGTH,"invalid val length argument" },
    { PMI_ERR_INVALID_LENGTH,   "invalid length argument" },
    { PMI_ERR_INVALID_NUM_ARGS, "invalid number of arguments" },
    { PMI_ERR_INVALID_ARGS,     "invalid args argument" },
    { PMI_ERR_INVALID_NUM_PARSED, "invalid num_parsed length argument" },
    { PMI_ERR_INVALID_KEYVALP,  "invalid keyvalp argument" },
    { PMI_ERR_INVALID_SIZE,     "invalid size argument" },
};
const int pmi_errors_len = sizeof (pmi_errors) / sizeof (pmi_errors[0]);

static void _fatal (int rank, int rc, const char *s)
{
    int i;

    for (i = 0; i < pmi_errors_len; i++) {
        if (pmi_errors[i].errnum == rc) {
            fprintf (stderr, "%d: %s: %s%s%s%s\n", rank, s,
                    pmi_errors[i].errstr,
                    errno > 0 ? " (" : "",
                    errno > 0 ? strerror (errno) : "",
                    errno > 0 ? ") " : "");
            break;
        }
    }
    if (i == pmi_errors_len)
        fprintf (stderr, "%d: %s: rc=%d\n", rank, s, rc);
    exit (1);
}

static void *xzmalloc (int rank, size_t size)
{
    void *obj = malloc (size);

    if (!obj) {
        fprintf (stderr, "%d: out of memory", rank);
        exit (1);
    }
    memset (obj, 0, size);

    return obj;
}

static void xgettimeofday (int rank, struct timeval *tv, struct timezone *tz)
{
    if (gettimeofday (tv, tz) < 0) {
        fprintf (stderr, "%d: gettimeofday: %s", rank, strerror (errno));
        exit (1);
    }
}

static void timesince (int rank,  struct timeval *start, const char *s)
{
    struct timeval end, t;

    xgettimeofday (rank, &end, NULL);
    timersub (&end, start, &t);
    fprintf (stderr, "%d: %s: %lu.%.3lu sec\n",
             rank, s, t.tv_sec, t.tv_usec / 1000);
}

#define OPTIONS "nN:"
static const struct option longopts[] = {
    {"n-squared",    no_argument,        0, 'n'},
    {"key-count",    required_argument,  0, 'N'},
    {0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
    struct timeval t;
    int id = -1, ntasks;
    int rc, spawned, kvsname_len, key_len, val_len;
    char *kvsname, *key, *val, *val2;
    bool nsquared = false;
    int ch;
    int i, j, keycount = 1;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'n':   /* --n-squared */
                nsquared = true;
                break;
            case 'N':   /* --key-count N */
                keycount = strtoul (optarg, NULL, 10);
                break;
        }
    }

    rc = PMI_Init (&spawned);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_init");
    rc = PMI_Get_size (&ntasks);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_Get_size");
    rc = PMI_Get_rank (&id);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_Get_rank");

    rc = PMI_KVS_Get_name_length_max (&kvsname_len);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_KVS_Get_name_length_max");
    kvsname = xzmalloc (id, kvsname_len);
    rc = PMI_KVS_Get_my_name (kvsname, kvsname_len);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_KVS_Get_my_name");

    rc = PMI_KVS_Get_key_length_max (&key_len);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_KVS_Get_key_length_max");
    key = xzmalloc (id, key_len);

    rc = PMI_KVS_Get_value_length_max (&val_len);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_KVS_Get_value_length_max");
    val = xzmalloc (id, val_len);
    val2 = xzmalloc (id, val_len);

    xgettimeofday (id, &t, NULL);

    /* keycount puts & one commit per rank */
    for (i = 0; i < keycount; i++) {
        snprintf (key, key_len, "kvstest-%d-%d", id, i);
        snprintf (val, val_len, "sandwich.%d.%d", id, i);
        rc = PMI_KVS_Put (kvsname, key, val);
        if (rc != PMI_SUCCESS)
            _fatal (id, rc, "PMI_KVS_Put");
    }
    rc = PMI_KVS_Commit (kvsname);
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_KVS_Commit");

    /* barrier */
    rc = PMI_Barrier ();
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_Barrier");

    if (id == 0)
        timesince (id, &t, "put phase");

    xgettimeofday (id, &t, NULL);

    /* keycount (or keycount*N) gets per rank */
    for (i = 0; i < keycount; i++) {
        if (nsquared) {
            for (j = 0; j < ntasks; j++) {
                snprintf (key, key_len, "kvstest-%d-%d", j, i);
                rc = PMI_KVS_Get (kvsname, key, val, val_len);
                if (rc != PMI_SUCCESS)
                    _fatal (id, rc, "PMI_KVS_Get");

                snprintf (val2, val_len, "sandwich.%d.%d", j, i);
                if (strcmp (val, val2) != 0) {
                    fprintf (stderr, "%d: PMI_KVS_Get: exp %s got %s\n",
                             id, val2, val);
                    return 1;
                }
            }
        } else {
            snprintf (key, key_len, "kvstest-%d-%d",
                      id > 0 ? id - 1 : ntasks - 1, i);
            rc = PMI_KVS_Get (kvsname, key, val, val_len);
            if (rc != PMI_SUCCESS)
                _fatal (id, rc, "PMI_KVS_Get");

            snprintf (val2, val_len, "sandwich.%d.%d",
                      id > 0 ? id - 1 : ntasks - 1, i);
            if (strcmp (val, val2) != 0) {
                fprintf (stderr, "%d: PMI_KVS_Get: exp %s got %s\n", id, val2, val);
                return 1;
            }
        }
    }

    /* barrier */
    rc = PMI_Barrier ();
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_Barrier");

    if (id == 0)
        timesince (id, &t, "get phase");

    rc = PMI_Finalize ();
    if (rc != PMI_SUCCESS)
        _fatal (id, rc, "PMI_Finalize");

    free (val);
    free (val2);
    free (key);
    free (kvsname);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
