/* pmiwrap.h - convenience wrappers for PMI API for flux internal use */

/* On error, pmi_init() calls msg_exit().
 * All other functions call PMI_Abort ().
 * pmi_kvs_get() of an unknown key is considered fatal (PMI_Abort)
 */

typedef struct pmi_struct *pmi_t;

pmi_t pmi_init (const char *libname);
void pmi_fini (pmi_t pmi);
void pmi_abort (pmi_t pmi, int rc, const char *fmt, ...);

int pmi_rank (pmi_t pmi);
int pmi_size (pmi_t pmi);
const char *pmi_id (pmi_t pmi);
int pmi_appnum (pmi_t pmi);

int pmi_clique_size (pmi_t pmi);
int pmi_clique_minrank (pmi_t pmi);

void pmi_kvs_put (pmi_t pmi, const char *val, const char *fmt, ...);
const char *pmi_kvs_get (pmi_t pmi, const char *fmt, ...);
void pmi_kvs_fence (pmi_t pmi);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
