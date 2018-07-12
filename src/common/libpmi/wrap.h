#ifndef _FLUX_CORE_PMI_WRAP_H
#define _FLUX_CORE_PMI_WRAP_H

#include "src/common/libpmi/pmi_operations.h"

void *pmi_wrap_create (const char *libname, struct pmi_operations **ops);

#endif /* _FLUX_CORE_PMI_WRAP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
