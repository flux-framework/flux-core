#ifndef __FLUX_CORE_FOP_H
#define __FLUX_CORE_FOP_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct fop_object_fake {
    void *fake[2];
};
struct fop_class_fake {
    void *fake[32];
};
#ifndef FOP_PROT
typedef struct fop_object_fake fop_object_t;
typedef struct fop_class_fake fop_class_t;
#else
typedef struct fop_object fop_object_t;
typedef struct fop_class fop_class_t;
#endif

// Flux object pointer
typedef void fop;

// class getters
const fop_class_t *fop_object_c ();
const fop_class_t *fop_class_c ();

// utility functions
fop_object_t *fop_cast_object (const fop *o);
void *fop_cast (const fop_class_t *c, const fop *o);
const void *fop_get_class (const fop *o);
const void *fop_get_class_checked (const fop *o, const fop_class_t *c);
const fop_class_t *fop_super (const fop_class_t *c);

bool fop_is_a (const fop *o, const fop_class_t *c);
bool fop_is_instance_of (const fop *o, const fop_class_t *c);

// fop_object_c selectors
// Every class is a subclass of fop_object_c, so these are pretty universal
fop *fop_alloc (const fop_class_t *c, size_t size);
fop *fop_new (const fop_class_t *c, ...);
fop *fop_initialize (fop *self, va_list *app);
void fop_finalize (fop *o);
void fop_retain (fop *o);
void fop_release (fop *o);
fop *fop_describe (fop *o, FILE *s);
fop *fop_represent (fop *o, FILE *s);
uintptr_t fop_hash (fop *);
bool fop_equal (const fop *, const fop *);
fop *fop_copy (fop *self, const fop *from);

// fop class construction routines

fop_class_t *fop_new_metaclass (const char *name,
                                const fop_class_t *parent,
                                size_t size);
fop *fop_new_class (const fop_class_t *metaclass,
                    const char *name,
                    const fop_class_t *parent,
                    size_t size);
typedef fop *(*fop_new_f) (const fop_class_t *, va_list *);
typedef fop *(*fop_init_f) (fop *, va_list *);
typedef void (*fop_fini_f) (void *);
typedef void (*fop_retain_f) (void *);
typedef void (*fop_release_f) (void *);
typedef fop *(*fop_putter_f) (fop *, FILE *);
typedef uintptr_t (*fop_hash_f) (fop *);
typedef bool (*fop_equal_f) (const fop *, const fop *);
typedef fop *(*fop_copy_f) (fop *self, const fop *from);

fop_class_t *fop_class_set_new (fop *c, fop_new_f fn);
fop_class_t *fop_class_set_init (fop *c, fop_init_f fn);
fop_class_t *fop_class_set_fini (fop *c, fop_fini_f fn);
fop_class_t *fop_class_set_describe (fop *c, fop_putter_f fn);
fop_class_t *fop_class_set_represent (fop *c, fop_putter_f fn);
fop_class_t *fop_class_set_retain (fop *c, fop_retain_f fn);
fop_class_t *fop_class_set_release (fop *c, fop_release_f fn);
fop_class_t *fop_class_set_hash (fop *c, fop_hash_f fn);
fop_class_t *fop_class_set_equal (fop *c, fop_equal_f fn);
fop_class_t *fop_class_set_copy (fop *c, fop_copy_f fn);

// Thread safety for class init
static inline bool fop_class_needs_init (fop_class_t **cls)
{
    void *oldval = NULL;
    void *newval = oldval + 1;
    if ((void*)*cls <= newval) {
        // TODO: add c11 standard version
        if (__sync_bool_compare_and_swap (cls, oldval, newval))
            return true;
        while (__sync_val_compare_and_swap ((void**)cls, oldval, newval) <= newval) {
            // spin
        }
    }
    return false;
}

// super helpers
void *fop_initialize_super (const fop_class_t *c, void *self, va_list *app);
void fop_finalize_super (const fop_class_t *c, void *o);

#endif /* __FLUX_CORE_FOP_H */
