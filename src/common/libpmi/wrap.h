#ifndef _FLUX_CORE_PMI_DLOPEN_WRAPPER_H
#define _FLUX_CORE_PMI_DLOPEN_WRAPPER_H

#include "src/common/libpmi/pmi.h"

struct pmi_wrap;

int pmi_wrap_init (struct pmi_wrap *pmi, int *spawned);
int pmi_wrap_initialized (struct pmi_wrap *pmi, int *initialized);
int pmi_wrap_finalize (struct pmi_wrap *pmi);
int pmi_wrap_get_size (struct pmi_wrap *pmi, int *size);
int pmi_wrap_get_rank (struct pmi_wrap *pmi, int *rank);
int pmi_wrap_get_appnum (struct pmi_wrap *pmi, int *appnum);
int pmi_wrap_get_universe_size (struct pmi_wrap *pmi, int *universe_size);
int pmi_wrap_publish_name (struct pmi_wrap *pmi,
                           const char *service_name, const char *port);
int pmi_wrap_unpublish_name (struct pmi_wrap *pmi, const char *service_name);
int pmi_wrap_lookup_name (struct pmi_wrap *pmi,
                          const char *service_name, char *port);
int pmi_wrap_barrier (struct pmi_wrap *pmi);
int pmi_wrap_abort (struct pmi_wrap *pmi, int exit_code, const char *error_msg);
int pmi_wrap_kvs_get_my_name (struct pmi_wrap *pmi, char *kvsname, int length);
int pmi_wrap_kvs_get_name_length_max (struct pmi_wrap *pmi, int *length);
int pmi_wrap_kvs_get_key_length_max (struct pmi_wrap *pmi, int *length);
int pmi_wrap_kvs_get_value_length_max (struct pmi_wrap *pmi, int *length);
int pmi_wrap_kvs_put (struct pmi_wrap *pmi, const char *kvsname,
                      const char *key, const char *value);
int pmi_wrap_kvs_commit (struct pmi_wrap *pmi, const char *kvsname);
int pmi_wrap_kvs_get (struct pmi_wrap *pmi, const char *kvsname,
                      const char *key, char *value, int len);
int pmi_wrap_get_clique_size (struct pmi_wrap *pmi, int *size);
int pmi_wrap_get_clique_ranks (struct pmi_wrap *pmi, int ranks[], int length);

int pmi_wrap_spawn_multiple (struct pmi_wrap *pmi,
                             int count,
                             const char *cmds[],
                             const char **argvs[],
                             const int maxprocs[],
                             const int info_keyval_sizesp[],
                             const PMI_keyval_t *info_keyval_vectors[],
                             int preput_keyval_size,
                             const PMI_keyval_t preput_keyval_vector[],
                             int errors[]);

void pmi_wrap_destroy (struct pmi_wrap *pmi);
struct pmi_wrap *pmi_wrap_create (const char *libname);


#endif /* _FLUX_CORE_PMI_DLOPEN_WRAPPER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
