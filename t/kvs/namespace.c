#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include <src/common/libutil/log.h>


const char *usage_str = "Usage: namespace {create|remove|lookup|commit} ...";

void cmd_create (flux_t *h, int argc, char **argv)
{
    if (argc != 3)
        log_msg_exit ("Usage: content create name userid flags");

    const char *name = argv[0];
    uint32_t userid = strtoul (argv[1], NULL, 0);
    int flags = strtoul (argv[2], NULL, 0);
    flux_future_t *f;

    if (!(f = flux_kvs_ns_create (h, FLUX_NODEID_ANY, name, userid, flags)))
        log_err_exit ("flux_kvs_ns_create");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");
    flux_future_destroy (f);
}

void cmd_remove (flux_t *h, int argc, char **argv)
{
    if (argc != 1)
        log_msg_exit ("Usage: content remove name");

    const char *name = argv[0];
    flux_future_t *f;

    if (!(f = flux_kvs_ns_remove (h, FLUX_NODEID_ANY, name)))
        log_err_exit ("flux_kvs_ns_remove");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");
    flux_future_destroy (f);
}

void cmd_lookup (flux_t *h, int argc, char **argv)
{
    if (argc != 3)
        log_msg_exit ("Usage: content lookup name min_seq flags");

    const char *name = argv[0];
    int min_seq = strtoul (argv[1], NULL, 10);
    int flags = strtoul (argv[2], NULL, 0);
    flux_future_t *f;
    int seq;
    const char *json_str;

    if (!(f = flux_kvs_ns_lookup (h, FLUX_NODEID_ANY, name, min_seq, flags)))
        log_err_exit ("flux_kvs_ns_lookup");
    if (flux_kvs_ns_lookup_get (f, &json_str) < 0)
        log_err_exit ("flux_kvs_ns_lookup");
    if (flux_kvs_ns_lookup_get_seq (f, &seq) < 0)
        log_err_exit ("flux_kvs_ns_lookup");
    printf ("%d %s\n", seq, json_str);
    flux_future_destroy (f);
}

void cmd_commit (flux_t *h, int argc, char **argv)
{
    if (argc != 3)
        log_msg_exit ("Usage: commit name seq obj");
    const char *name = argv[0];
    int seq = strtoul (argv[1], NULL, 10);
    const char *json_str = argv[2];
    flux_future_t *f;

    if (!(f = flux_kvs_ns_commit (h, FLUX_NODEID_ANY, name, seq, json_str)))
        log_err_exit ("flux_kvs_ns_commit");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");
    printf ("%d %s\n", seq, json_str);
    flux_future_destroy (f);
}

int main (int argc, char *argv[])
{
    log_init ("namespace");
    if (argc < 2)
        log_msg_exit ("%s\n", usage_str);
    flux_t *h = flux_open (NULL, 0);
    if (!h)
        log_err_exit ("flux_open");

    if (!strcmp (argv[1], "create")) {
        cmd_create (h, argc - 2, argv + 2);
    }
    else if (!strcmp (argv[1], "remove")) {
        cmd_remove (h, argc - 2, argv + 2);
    }
    else if (!strcmp (argv[1], "lookup")) {
        cmd_lookup (h, argc - 2, argv + 2);
    }
    else if (!strcmp (argv[1], "commit")) {
        cmd_commit (h, argc - 2, argv + 2);
    }
    else {
        log_msg_exit ("%s\n", usage_str);
    }

    flux_close (h);
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
