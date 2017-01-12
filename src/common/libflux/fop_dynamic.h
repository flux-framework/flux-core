#ifndef __FLUX_CORE_FOP_DYNAMIC_H
#define __FLUX_CORE_FOP_DYNAMIC_H

#include "fop_protected.h"
typedef void (*fop_vv_f) (void);
const fop_class_t *fop_interface_c ();
const void *fop_get_interface (const fop *o, const fop_class_t *interface);
fop_vv_f fop_get_method (const fop_class_t *c, const char *name);
fop_vv_f fop_get_method_by_sel (const fop_class_t *c, fop_vv_f sel);
int fop_register_method (fop_class_t *c,
                         fop_vv_f selector,
                         const char *name,
                         size_t offset,
                         fop_vv_f fn);
int fop_implement_method (fop_class_t *c, const char *tag, fop_vv_f method);
int fop_implement_method_by_sel (fop_class_t *c, fop_vv_f sel, fop_vv_f method);
struct fop_method_record {
    const char *tag;
    fop_vv_f selector;
    fop_vv_f method;
    size_t offset;
};
int fop_register_and_implement_methods (fop_class_t *c,
                                        struct fop_method_record *records);

void fop_dynamic_class_init (fop_class_t *c, const fop_class_t *super);


#endif /* __FLUX_CORE_FOP_DYNAMIC_H */
