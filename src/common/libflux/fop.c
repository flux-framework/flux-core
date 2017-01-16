#include "fop_protected.h"

#include "fop_dynamic.h"

#include "src/common/libutil/sds.h"

#include <Judy.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum fop_magic { magic = 0xdeadbeef };
static fop_class_t __Class;
static fop_class_t __Object;

// Statically bound utility functions
fop_object_t *fop_cast_object (const void *o)
{
    if (!o)
        return NULL;
    const fop_object_t *obj = o;
    if (obj->magic != (int)magic)
        return NULL;
    return (fop_object_t *)o;
}

void *fop_cast (const fop_class_t *c, const void *o)
{
    // TODO: lots of extra error checking can go here
    fop_object_t *obj = fop_cast_object (o);
    if (!obj)
        return NULL;
    // If we've gotten this far, it has to descend from object
    if (c == fop_object_c ())
        return (void *)o;
    const fop_class_t *cur = obj->fclass;
    while (cur != c) {
        if (cur == fop_object_c ())
            return NULL;  // no match found
        cur = cur->super;
    }

    return (void *)o;
}

const void *fop_get_class (const void *o)
{
    fop_object_t *obj = fop_cast_object ((void *)o);
    return obj ? obj->fclass : NULL;
}

const void *fop_get_class_checked (const void *o, const fop_class_t *c)
{
    fop_object_t *obj = fop_cast (c, (void *)o);
    return obj ? obj->fclass : NULL;
}

const fop_class_t *fop_super (const fop_class_t *c)
{
    c = fop_cast (fop_class_c (), c);
    assert (c && c->super);
    return c->super;
}

bool fop_is_a (const void *o, const fop_class_t *c)
{
    fop_object_t *obj = fop_cast_object ((void *)o);
    const fop_class_t *cls = fop_cast (fop_class_c (), (void *)c);
    if (!obj || !cls)
        return false;
    return fop_get_class (obj) == cls;
}

bool fop_is_instance_of (const void *o, const fop_class_t *c)
{
    if (c == fop_object_c ())
        return true;
    fop_object_t *obj = fop_cast (c, (void *)o);
    return obj != NULL;
}

void fop_tag_object (fop_object_t *o, const fop_class_t *c)
{
    o->magic = magic;
    o->refcount = 1;
    o->fclass = c;
}

void *fop_alloc (const fop_class_t *c, size_t size)
{
    assert (c->size);
    fop_object_t *o = calloc (1, size ? size : c->size);
    if (!o) {
        return NULL;
    }
    fop_tag_object (o, c);
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
    fop_object_t *result = c->new ? c->new (c, &ap) : NULL;
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
void *fop_initialize_super (const fop_class_t *c, void *self, va_list *app)
{
    if (!self)
        return NULL;
    const fop_class_t *superclass = fop_super (c);
    assert (superclass);
    return superclass->initialize ? superclass->initialize (self, app) : self;
};
void fop_finalize (void *o)
{
    if (!o)
        return;
    const fop_class_t *c = fop_get_class (o);
    if (c && c->finalize)
        c->finalize (o);
};
void fop_finalize_super (const fop_class_t *c, void *o)
{
    if (!o)
        return;
    const fop_class_t *superclass = fop_super (c);
    if (superclass && superclass->finalize)
        superclass->finalize (o);
};
void fop_retain (void *o)
{
    if (!o)
        return;
    const fop_class_t *c = fop_get_class (o);
    if (c && c->retain)
        c->retain (o);
};
void fop_release (void *o)
{
    if (!o)
        return;
    const fop_class_t *c = fop_get_class (o);
    if (c && c->release)
        c->release (o);
};
fop *fop_describe (void *o, FILE *s)
{
    const fop_class_t *c = fop_get_class (o);
    if (!c)
        return NULL;
    if (!c->describe) {
        return fop_represent (o, s);
    }
    return c->describe (o, s);
};
fop *fop_represent (void *o, FILE *s)
{
    const fop_class_t *c = fop_get_class (o);
    if (!c)
        return NULL;
    return c->represent ? c->represent (o, s) : NULL;
};

// Class construction routines
fop_class_t *fop_new_metaclass (const char *name,
                                const fop_class_t *parent,
                                size_t size)
{
    return fop_new (fop_class_c (), name, parent, size);
}

fop *fop_new_class (const fop_class_t *metaclass,
                            const char *name,
                            const fop_class_t *parent,
                            size_t size)
{
    return fop_new (metaclass, name, parent, size);
}

fop_class_t *fop_class_set_new (fop *_c, fop_new_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->new = fn;
    return c;
}
fop_class_t *fop_class_set_init (fop *_c, fop_init_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->initialize = fn;
    return c;
}
fop_class_t *fop_class_set_fini (fop *_c, fop_fini_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->finalize = fn;
    return c;
}
fop_class_t *fop_class_set_describe (fop *_c, fop_putter_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->describe = fn;
    return c;
}
fop_class_t *fop_class_set_represent (fop *_c, fop_putter_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->represent = fn;
    return c;
}
fop_class_t *fop_class_set_retain (fop *_c, fop_retain_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->retain = fn;
    return c;
}
fop_class_t *fop_class_set_release (fop *_c, fop_release_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->release = fn;
    return c;
}
fop_class_t *fop_class_set_hash (fop *_c, fop_hash_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->hash = fn;
    return c;
}
fop_class_t *fop_class_set_equal (fop *_c, fop_equal_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->equal = fn;
    return c;
}
fop_class_t *fop_class_set_copy (fop *_c, fop_copy_f fn)
{
    fop_class_t * c = fop_cast (fop_class_c (), _c);
    if (!c) {
        return NULL;
    }
    c->copy = fn;
    return c;
}

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
void object_finalize (void *o){
    // Nothing to do, release does the free
    (void)(o);
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
        fop_finalize (obj);
        // muss up magic to keep this from being reused
        obj->magic = 0xeeeeeeee;
        free (obj);
    }
};
fop *object_represent (fop *o, FILE *s)
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

uintptr_t object_hash (fop *o)
{
    // use the address as a hash when all else fails
    return (uintptr_t) o;
}

bool object_equal (const fop *l, const fop *r)
{
    // use the addresses if we ended up here
    return l == r;
}

// Class methods

void *class_initialize (fop *c_in, va_list *app)
{
    fop_class_t *c = c_in;
    const char *name = va_arg (*app, char *);
    const fop_class_t *super = va_arg (*app, fop_class_t *);
    size_t size = va_arg (*app, size_t);
    size_t offset = offsetof (fop_class_t, new);

    const fop_class_t *super_metaclass = fop_get_class (super);
    /* fprintf (stderr, "inheriting %s(%zu) from %s(%zu), meta %s(%zu)\n", name,
     */
    /*          size, super->name, super->size, super_metaclass->name, */
    /*          super_metaclass->size); */
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

void *class_desc (fop *c_in, FILE *s)
{
    fop_class_t *c = c_in;
    fprintf (s, "class %s(%s)\n", c->name, fop_super (c)->name);
    fprintf (s, "    size: %zu\n", c->size);
    size_t nif =
        c->inner.interfaces
            ? sdslen ((void *)c->inner.interfaces) / sizeof (struct iface_pair)
            : 0;
    fprintf (s, "    interfaces: %zu\n", nif);

    for (size_t i = 0; i < nif; ++i) {
        struct iface_pair *rec = c->inner.interfaces + i;
        fprintf (s, "        %s: %zu\n", rec->iface->name, rec->offset);
    }
    fprintf (s, "    default methods:\n");
    fprintf (s, "        new: %s\n",
             c->new ? (c->new == object_new ? "default" : "custom") : "unimplem"
                                                                      "ented");
    fprintf (s, "        initialize: %s\n",
             c->initialize
                 ? (c->initialize == object_initialize ? "default" : "custom")
                 : "unimplemented");
    fprintf (s, "        finalize: %s\n",
             c->finalize
                 ? (c->finalize == object_finalize ? "default" : "custom")
                 : "unimplemented");
    fprintf (s, "        describe: %s\n", c->describe ? "custom" : "unimplement"
                                                                   "ed");
    fprintf (s, "        represent: %s\n",
             c->represent
                 ? (c->represent == object_represent ? "default" : "custom")
                 : "unimplemented");
    fprintf (s, "        retain: %s\n",
             c->retain ? (c->retain == object_retain ? "default" : "custom")
                       : "unimplemented");
    fprintf (s, "        release: %s\n",
             c->release ? (c->release == object_release ? "default" : "custom")
                        : "unimplemented");
    return c_in;
};

// Object and Class definition, static for this special case

static fop_class_t __Object = {

    {
        // object _
        magic,      // magic
        INT32_MAX,  // refcount
        &__Class    // class
    },
    "Object",                // name
    &__Object,               // superclass
    sizeof (fop_object_t),  // size
    {0},                     // Inner
    object_new,
    object_initialize,
    object_finalize,
    0,  // describe
    object_represent,
    object_retain,
    object_release,
    object_hash,
    object_equal,
    0}; // copy, no default

static fop_class_t __Class = {

    {
        // object _
        magic,      // magic
        INT32_MAX,  // refcount
        &__Class    // class
    },
    "Class",                 // name
    &__Object,               // superclass
    sizeof (fop_class_t),  // size
    {0},                     // Inner
    object_new,
    class_initialize,
    0,           // finalize
    class_desc,  // describe
    object_represent,
    0,  // retain
    0,   // release
    object_hash,
    object_equal,
    0 }; // no default copy

// object and class class getters
const fop_class_t *fop_object_c ()
{
    return &__Object;
}
const fop_class_t *fop_class_c ()
{
    return &__Class;
}
