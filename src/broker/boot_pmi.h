#ifndef BROKER_BOOT_PMI_H
#define BROKER_BOOT_PMI_H

/* boot_pmi - bootstrap broker/overlay with PMI */

#include "attr.h"
#include "overlay.h"

int boot_pmi (overlay_t *overlay, attr_t *attrs, int tbon_k);

#endif /* BROKER_BOOT_PMI_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
