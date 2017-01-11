#ifndef __FLUX_CORE_FOP_H
#define __FLUX_CORE_FOP_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct object fop_object_t;
typedef struct fclass fop_class_t;

// Flux object pointer
typedef void fop;

// class getters
const fop_class_t *fop_object_c ();
const fop_class_t *fop_class_c ();

// utility functions
fop_object_t *fop_cast_object (fop *o);
void *fop_cast (const fop_class_t *c, fop *o);
const void *fop_get_class (const fop *o);
const void *fop_get_class_checked (const fop *o, const fop_class_t *c);

bool fop_is_a (const fop *o, const fop_class_t *c);
bool fop_is_instance_of (const fop *o, const fop_class_t *c);

// fop_object_c selectors
// Every class is a subclass of fop_object_c, so these are pretty universal
fop *fop_new (const fop_class_t *c, ...);
fop *fop_initialize (fop *self, va_list *app);
void fop_finalize (fop *o);
void fop_retain (fop *o);
void fop_release (fop *o);
fop * fop_describe (fop *o, FILE *s);
fop * fop_represent (fop *o, FILE *s);

#endif /* __FLUX_CORE_FOP_H */
