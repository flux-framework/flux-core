#ifndef _FLUX_CORE_PMI_CLIENT_H
#define _FLUX_CORE_PMI_CLIENT_H

#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"

typedef void (*pmi_free_f)(void *impl);

typedef struct pmi_struct pmi_t;

/* Create/destroy pmi_t class.
 */
void pmi_destroy (pmi_t *pmi);

pmi_t *pmi_create_dlopen (const char *filename);
pmi_t *pmi_create_simple (void);
pmi_t *pmi_create_guess (void);

/* Functions below match the canonical PMIv1 ABI, except for
 * the pmi_t first argument and the lower case function names.
 */

int pmi_init (pmi_t *pmi, int *spawned);
int pmi_initialized (pmi_t *pmi, int *initialized);
int pmi_finalize (pmi_t *pmi);
int pmi_get_size (pmi_t *pmi, int *size);
int pmi_get_rank (pmi_t *pmi, int *rank);
int pmi_get_universe_size (pmi_t *pmi, int *size);
int pmi_get_appnum (pmi_t *pmi, int *appnum);
int pmi_barrier (pmi_t *pmi);
int pmi_abort (pmi_t *pmi, int exit_code, const char *error_msg);
int pmi_kvs_get_my_name (pmi_t *pmi, char *kvsname, int length);
int pmi_kvs_get_name_length_max (pmi_t *pmi, int *length);
int pmi_kvs_get_key_length_max (pmi_t *pmi, int *length);
int pmi_kvs_get_value_length_max (pmi_t *pmi, int *length);
int pmi_kvs_put (pmi_t *pmi,
        const char *kvsname, const char *key, const char *value);
int pmi_kvs_commit (pmi_t *pmi, const char *kvsname);
int pmi_kvs_get (pmi_t *pmi,
        const char *kvsname, const char *key, char *value, int len);

int pmi_get_clique_size (pmi_t *pmi, int *size);
int pmi_get_clique_ranks (pmi_t *pmi, int *ranks, int length);

#endif /* ! _FLUX_CORE_PMI_CLIENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
