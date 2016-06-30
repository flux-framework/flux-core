#ifndef _FLUX_CORE_PMI_CLIENT_IMPL_H
#define _FLUX_CORE_PMI_CLIENT_IMPL_H

typedef int (*pmi_init_f)(void *impl, int *spawned);
typedef int (*pmi_finalize_f)(void *impl);

typedef int (*pmi_get_int_f)(void *impl, int *size);

typedef int (*pmi_barrier_f)(void *impl);
typedef int (*pmi_abort_f)(void *impl, int exit_code, const char *error_msg);
typedef int (*pmi_kvs_get_my_name_f)(void *impl, char *kvsname, int length);
typedef int (*pmi_kvs_put_f)(void *impl, const char *kvsname,
                             const char *key, const char *value);
typedef int (*pmi_kvs_commit_f)(void *impl, const char *kvsname);
typedef int (*pmi_kvs_get_f)(void *impl, const char *kvsname,
                             const char *key, char *value, int len);
typedef int (*pmi_get_clique_ranks_f)(void *impl, int *ranks, int length);


struct pmi_struct {
    pmi_init_f init;
    pmi_get_int_f initialized;
    pmi_finalize_f finalize;

    pmi_get_int_f get_size;
    pmi_get_int_f get_rank;
    pmi_get_int_f get_universe_size;
    pmi_get_int_f get_appnum;

    pmi_barrier_f barrier;
    pmi_abort_f abort;

    pmi_get_int_f kvs_get_name_length_max;
    pmi_get_int_f kvs_get_key_length_max;
    pmi_get_int_f kvs_get_value_length_max;

    pmi_kvs_get_my_name_f kvs_get_my_name;
    pmi_kvs_put_f kvs_put;
    pmi_kvs_commit_f kvs_commit;
    pmi_kvs_get_f kvs_get;

    pmi_get_int_f get_clique_size;
    pmi_get_clique_ranks_f get_clique_ranks;

    // internal
    void *impl;
    pmi_free_f impl_destroy;
};

pmi_t *pmi_create (void *impl, pmi_free_f destroy);

#endif /* ! _FLUX_CORE_PMI_CLIENT_IMPL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
