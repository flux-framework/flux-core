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
#include "src/common/libpmi-client/pmi-client.h"

#define OPTIONS "l:"
static const struct option longopts[] = {
    {"library",      required_argument,  0, 'l'},
    {0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
    int rank, size, appnum;
    int e, spawned, initialized, kvsname_len, key_len, val_len;
    char *kvsname;
    int ch;
    char *library = NULL;
    pmi_t *pmi;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'l':   /* --library */
                library = optarg;
                break;
        }
    }
    if (library)
        pmi = pmi_create_dlopen (library);
    else
        pmi = pmi_create_guess ();
    if (!pmi)
        log_err_exit ("pmi_create");
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
    e = pmi_get_appnum (pmi, &appnum);
    if (e != PMI_SUCCESS)
        log_msg_exit ("pmi_get_appnum: %s", pmi_strerror (e));

    kvsname = xzmalloc (kvsname_len);
    e = pmi_kvs_get_my_name (pmi, kvsname, kvsname_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_kvs_get_my_name: %s", rank, pmi_strerror (e));

    printf ("%d: size=%d appnum=%d maxes=%d:%d:%d kvsname=%s\n",
            rank, size, appnum, kvsname_len, key_len, val_len, kvsname);

    e = pmi_finalize (pmi);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: pmi_finalize: %s", rank, pmi_strerror (e));

    free (kvsname);
    pmi_destroy (pmi);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
