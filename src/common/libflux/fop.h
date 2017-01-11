#ifndef __FLUX_CORE_FOP_H
#define __FLUX_CORE_FOP_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct fake_object {
    int64_t fake[2];
};
typedef void (*fop_vv_f) (void);
struct fake_class {
    struct fake_object fake1;
    void *fake2[2];
    size_t fake3[1];
    struct {
        size_t meh1[2];
        void *meh2;
        bool bah;
    } fake5;
    fop_vv_f fake4[7];
};
#ifndef FOP_PROT
typedef struct fake_object fop_object_t;
#else
typedef struct object fop_object_t;
#endif
#ifndef FOP_PROT
typedef struct fake_class fop_class_t;
#else
typedef struct fclass fop_class_t;
#endif

// class getters
const fop_class_t *fop_object_c ();
const fop_class_t *fop_class_c ();

// utility functions
fop_object_t *fop_cast_object (void *o);
void *fop_cast (const fop_class_t *c, void *o);
const fop_class_t *fop_get_class (const void *o);
bool fop_is_a (const void *o, const fop_class_t *c);
bool fop_is_instance_of (const void *o, const fop_class_t *c);

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

// fop_object_c selectors
// Every class is a subclass of fop_object_c, so these are pretty universal
void *fop_new (const fop_class_t *c, ...);
void *fop_initialize (void *self, va_list *app);
void *fop_finalize (void *o);
int fop_retain (void *o);
int fop_release (void *o);
int fop_describe (void *o, FILE *s);
int fop_represent (void *o, FILE *s);

#endif /* __FLUX_CORE_FOP_H */
