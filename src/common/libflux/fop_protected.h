#ifndef __FLUX_CORE_FOP_PROT_H
#define __FLUX_CORE_FOP_PROT_H
#define FOP_PROT
#include "fop.h"
#undef FOP_PROT

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

void fop_tag_object (fop_object_t *o, const fop_class_t *c);
struct fop_object {
    int32_t magic;
    int32_t refcount;
    const fop_class_t *fclass;
};

typedef void (*self_only_v_f) (void *);
typedef int (*self_only_i_f) (void *);
typedef fop *(*self_only_p_f) (fop *);

struct fclass;

struct iface_pair {
    const fop_class_t *iface;
    void *impl;
    size_t offset;
};

struct fclass_inner {
    struct iface_pair *interfaces;
};

struct fop_class {
    fop_object_t _;  // A class is also an object
    const char *name;
    const fop_class_t *super;
    size_t size;
    struct fclass_inner inner;

    fop_new_f new;
    fop_init_f initialize;
    fop_fini_f finalize;
    fop_putter_f describe;
    fop_putter_f represent;
    fop_retain_f retain;
    fop_release_f release;
    fop_hash_f hash;
    fop_equal_f equal;
    fop_copy_f copy;
};

#endif /* __FLUX_CORE_FOP_PROT_H */
