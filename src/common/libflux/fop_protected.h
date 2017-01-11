#ifndef __FLUX_CORE_FOP_PROT_H
#define __FLUX_CORE_FOP_PROT_H
#include "fop.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

struct object {
    int32_t magic;
    int32_t refcount;
    const fop_class_t *fclass;
};

typedef void (*self_only_v_f) (void *);
typedef int (*self_only_i_f) (void *);
typedef fop *(*self_only_p_f) (fop *);
typedef fop *(*new_f) (const fop_class_t *, va_list *);
typedef fop *(*init_f) (fop *, va_list *);
typedef fop *(*putter_f) (fop *, FILE *);

struct fclass_inner {
    size_t tags_len;
    size_t tags_cap;
    struct fop_method_record * tags_by_selector;
    bool sorted;
};

struct fclass {
    struct object _;  // A class is also an object
    const char *name;
    const fop_class_t *super;
    size_t size;
    struct fclass_inner inner;

    new_f new;
    init_f initialize;
    self_only_v_f finalize;
    putter_f describe;
    putter_f represent;
    self_only_v_f retain;
    self_only_v_f release;
};

#endif /* __FLUX_CORE_FOP_PROT_H */
