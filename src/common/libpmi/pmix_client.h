#ifndef _FLUX_CORE_PMI_PMIX_CLIENT_H
#define _FLUX_CORE_PMI_PMIX_CLIENT_H

#include "src/common/libpmi/pmi_operations.h"

void *pmix_client_create (struct pmi_operations **ops);


#endif /* _FLUX_CORE_PMI_PMIX_CLIENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
