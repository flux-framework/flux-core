#ifndef __FLUX_CORE_FOP_DYNAMIC_H
#define __FLUX_CORE_FOP_DYNAMIC_H

#include "fop.h"
typedef struct fop_interface {
    fop_object_t _;
} fop_interface_t;
const fop_class_t *fop_interface_c ();
const void *fop_get_interface (const fop *o, const fop_class_t *interface);
void fop_implement_interface (fop_class_t *c,
                              const fop_class_t *interface,
                              size_t offset);
void fop_implement_dynamic_interface (fop_class_t *c,
                                      const fop_class_t *interface,
                                      fop_interface_t *impl);

void fop_dynamic_class_init (fop_class_t *c, const fop_class_t *super);

#endif /* __FLUX_CORE_FOP_DYNAMIC_H */
