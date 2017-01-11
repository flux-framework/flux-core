#include "fop_protected.h"
#include "fop_dynamic.h"

#include <assert.h>

#include <Judy.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum fop_magic {
    magic =  0xdeadbeef
};
static struct fclass __Class;
static struct fclass __Object;

// Statically bound utility functions
fop_object_t *fop_cast_object (void *o)
{
    if (!o)
        return NULL;
    struct object *obj = o;
    if (obj->magic != (int)magic)
        return NULL;
    return o;
}

void *fop_cast (const fop_class_t *c, void *o)
{
    // TODO: lots of extra error checking can go here
    struct object *obj = fop_cast_object (o);
    if (!obj)
        return NULL;
    // If we've gotten this far, it has to descend from object
    if (c == fop_object_c ())
        return o;
    const fop_class_t *cur = obj->fclass;
    while (cur != c) {
        if (cur == fop_object_c ())
            return NULL;  // no match found
        cur = cur->super;
    }

    return o;
}

const void *fop_get_class (const void *o)
{
    struct object *obj = fop_cast_object ((void *)o);
    return obj ? obj->fclass : NULL;
}

const void *fop_get_class_checked (const void *o, const fop_class_t *c)
{
    struct object *obj = fop_cast (c, (void *)o);
    return obj ? obj->fclass : NULL;
}

bool fop_is_a (const void *o, const fop_class_t *c)
{
    struct object *obj = fop_cast_object ((void *)o);
    const fop_class_t *cls = fop_cast (fop_class_c (), (void *)c);
    if (!obj || !cls)
        return false;
    return fop_get_class (obj) == cls;
}

bool fop_is_instance_of (const void *o, const fop_class_t *c)
{
    if (c == fop_object_c ())
        return true;
    struct object *obj = fop_cast (c, (void *)o);
    return obj != NULL;
}

void *fop_alloc (const fop_class_t *c, size_t size)
{
    assert (c->size);
    fop_object_t *o = calloc (1, size ? size : c->size);
    if (!o) {
        return NULL;
    }
    o->magic = magic;
    o->refcount = 1;
    o->fclass = c;
    return o;
}

// selectors
void *fop_new (const fop_class_t *c, ...)
{
    if (!c)
        return NULL;
    va_list ap;
    assert (c);
    va_start (ap, c);
    struct object *result = c->new ? c->new (c, &ap) : NULL;
    va_end (ap);
    return result;
};

void *fop_initialize (void *self, va_list *app)
{
    if (!self)
        return NULL;
    const fop_class_t *c = fop_get_class (self);
    assert (c);
    return c->initialize ? c->initialize (self, app) : self;
};
void fop_finalize (void *o)
{
    if (!o) return;
    const fop_class_t *c = fop_get_class (o);
    if (c && c->finalize )
        c->finalize (o);
};
void fop_retain (void *o)
{
    if (!o) return;
    const fop_class_t *c = fop_get_class (o);
    if (c && c->retain )
        c->retain (o);
};
void fop_release (void *o)
{
    if (!o) return;
    const fop_class_t *c = fop_get_class (o);
    if (c && c->release )
        c->release (o);
};
fop * fop_describe (void *o, FILE *s)
{
    const fop_class_t *c = fop_get_class (o);
    if (!c)
        return NULL;
    if (!c->describe) {
        return fop_represent (o, s);
    }
    return c->describe (o, s);
};
fop * fop_represent (void *o, FILE *s)
{
    const fop_class_t *c = fop_get_class (o);
    if (!c)
        return NULL;
    return c->represent ? c->represent (o, s) : NULL;
};

// Object methods

void *object_initialize (void *o, va_list *app)
{
    (void)(app);  // silence unused warning...
    if (!fop_cast_object (o)) {
        return NULL;
    }
    return o;
};
void *object_new (const fop_class_t *c, va_list *app)
{
    void *buf = fop_alloc (c, 0);
    if (!buf)
        return NULL;
    void *res = fop_initialize (buf, app);
    if (!res) {
        // XXX: it's arguable if this should be release or not, since init
        // failed, it may not be valid to release it and call finalize, but
        // not doing so may leave it partially allocated internally.
        // auto-release pools would help a lot here, but one thing at a
        // time...
        free (buf);
    }
    return res;
};
void object_finalize (void *o)
{
    // Nothing to do, release does the free
};
void object_retain (void *o)
{
    fop_object_t *obj = fop_cast_object (o);
    if (!obj) {
        return;
    }
    assert (obj->refcount < 1 << 30);
    obj->refcount++;
};
void object_release (void *o)
{
    fop_object_t *obj = fop_cast_object (o);
    if (!obj) {
        return;
    }
    assert (obj->refcount > 0);
    if (--obj->refcount == 0) {
        fprintf (stderr, "finalizing: ");
        fop_describe (o, stderr);
        fprintf (stderr, "\n");
        fop_finalize (o);
        free (o);
    }
};
fop * object_represent (fop *o, FILE *s)
{
    fop_object_t *obj = fop_cast_object (o);
    if (!obj) {
        fprintf (s, "<unknown ptr@%p>", o);
    } else {
        const fop_class_t *c = fop_get_class (obj);
        fprintf (s, "<%s@%p>", c->name, obj);
    }
    return o;
};

// Class methods

void *class_initialize (fop *c_in, va_list *app)
{
    fop_class_t *c = c_in;
    const char *name = va_arg (*app, char *);
    const fop_class_t *super = va_arg (*app, fop_class_t *);
    size_t size = va_arg (*app, size_t);
    size_t offset = offsetof (fop_class_t, new);

    const fop_class_t *super_metaclass = fop_get_class (super);
    fprintf (stderr, "inheriting %s(%zu) from %s(%zu), meta %s(%zu)\n", name,
             size, super->name, super->size, super_metaclass->name,
             super_metaclass->size);
    if (super != NULL && super != c && super->size) {
        fop_dynamic_class_init (c, super);
        // inheritence...
        memcpy (((char *)c) + offset, ((char *)super) + offset,
                super_metaclass->size - offset);
    }
    c->name = name;
    c->super = super;
    c->size = size;

    return c;
};

// Object and Class definition, static for this special case

// DYNAMIC METHOD INFORMATION
static struct fop_method_record object_tags[] = {
    {"new", (fop_vv_f)fop_new, (fop_vv_f)object_new,
     offsetof (fop_class_t, new)},
    {"initialize", (fop_vv_f)fop_initialize, (fop_vv_f)object_initialize,
     offsetof (fop_class_t, initialize)},
    {"finalize", (fop_vv_f)fop_finalize, (fop_vv_f)object_finalize,
     offsetof (fop_class_t, finalize)},
    {"describe", (fop_vv_f)fop_describe, 0,
     offsetof (fop_class_t, describe)},  // describe
    {"represent", (fop_vv_f)fop_represent, (fop_vv_f)object_represent,
     offsetof (fop_class_t, represent)},
    {"retain", (fop_vv_f)fop_retain, (fop_vv_f)object_retain,
     offsetof (fop_class_t, retain)},
    {"release", (fop_vv_f)fop_release, (fop_vv_f)object_release,
     offsetof (fop_class_t, release)}};

static struct fop_method_record class_tags[] = {
        {"new", (fop_vv_f)fop_new, (fop_vv_f)object_new,
                offsetof (fop_class_t, new)},
        {"initialize", (fop_vv_f)fop_initialize, (fop_vv_f)object_initialize,
                offsetof (fop_class_t, initialize)},
        {"finalize", (fop_vv_f)fop_finalize, (fop_vv_f)object_finalize,
                offsetof (fop_class_t, finalize)},
        {"describe", (fop_vv_f)fop_describe, 0,
                offsetof (fop_class_t, describe)},  // describe
        {"represent", (fop_vv_f)fop_represent, (fop_vv_f)object_represent,
                offsetof (fop_class_t, represent)},
        {"retain", (fop_vv_f)fop_retain, (fop_vv_f)object_retain,
                offsetof (fop_class_t, retain)},
        {"release", (fop_vv_f)fop_release, (fop_vv_f)object_release,
                offsetof (fop_class_t, release)}};
// END DYNAMIC METHOD INFORMATION


static struct fclass __Object = {

    {
        // object _
        magic,      // magic
        INT32_MAX,  // refcount
        &__Class    // class
    },
    "Object",                // name
    &__Object,               // superclass
    sizeof (struct object),  // size
    {sizeof (object_tags) / sizeof (struct fop_method_record),
     sizeof (object_tags) / sizeof (struct fop_method_record), object_tags,
     0},  // Inner
    object_new,
    object_initialize,
    object_finalize,
    0,  // describe
    object_represent,
    object_retain,
    object_release};

static struct fclass __Class = {

    {
        // object _
        magic,      // magic
        INT32_MAX,  // refcount
        &__Class    // class
    },
    "Class",                 // name
    &__Object,               // superclass
    sizeof (struct fclass),  // size
    {sizeof (object_tags) / sizeof (struct fop_method_record),
     sizeof (object_tags) / sizeof (struct fop_method_record), class_tags,
     0},  // Inner
    object_new,
    class_initialize,
    0,  // finalize
    0,  // describe
    object_represent,
    0,  // retain
    0   // release
};

// object and class class getters
const fop_class_t *fop_object_c ()
{
    return &__Object;
}
const fop_class_t *fop_class_c ()
{
    return &__Class;
}
