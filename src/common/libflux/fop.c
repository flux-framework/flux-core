#include "fop_protected.h"

_Static_assert(sizeof (struct fake_object) == sizeof (struct object),
               "object sizes must match");
_Static_assert(sizeof (struct fake_class) == sizeof (struct fclass),
               "class sizes must match");

#include <Judy.h>

#include <assert.h>
#include <errno.h>
#include <search.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum fop_magic {
    magic =  0xdeadbeef
};
static struct fclass __Class;
static struct fclass __Object;

static int cmp_frm (const void *l, const void *r)
{
    const struct fop_method_record *lhs = l, *rhs = r;
    if (lhs && rhs && lhs->tag && rhs->tag)
        return strcmp (lhs->tag, rhs->tag);
    else
        return -1;
}

static int cmp_frm_sel (const void *l, const void *r)
{
    const struct fop_method_record *lhs = l, *rhs = r;
    if ((uintptr_t) lhs->selector < (uintptr_t) rhs->selector)
        return -1;
    if ((uintptr_t) lhs->selector == (uintptr_t) rhs->selector)
        return 0;
    return 1;
}

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

const fop_class_t *fop_get_class (const void *o)
{
    struct object *obj = fop_cast_object ((void *)o);
    return obj ? obj->fclass : NULL;
}

const fop_class_t *fop_get_class_checked (const fop_class_t *c, const void *o)
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

typedef void (*fop_vv_f) (void);
int fop_implement_method (fop_class_t *c, const char *tag, fop_vv_f method)
{
    struct fop_method_record matcher = {.tag = tag};
    size_t nel = c->inner.tags_len;
    struct fop_method_record *r =
        bsearch (&matcher, c->inner.tags_by_selector, nel,
               sizeof *r, cmp_frm);
    assert (r);
    r->method = method;
    if (r->offset) {
        fop_vv_f *target = (((void *)c) + r->offset);
        *target = method;
    }

    return 0;
}
int fop_implement_method_by_sel (fop_class_t *c, fop_vv_f sel, fop_vv_f method)
{
    struct fop_method_record matcher = {.selector = sel};
    size_t nel = c->inner.tags_len;
    struct fop_method_record *r =
        lfind (&matcher, c->inner.tags_by_selector, &nel,
               sizeof *r, cmp_frm_sel);
    assert (r);
    r->method = method;
    if (r->offset) {
        fop_vv_f *target = (((void *)c) + r->offset);
        *target = method;
    }

    return 0;
}
int fop_register_method (fop_class_t *c,
                         fop_vv_f selector,
                         const char *name,
                         size_t offset,
                         fop_vv_f fn)
{
    struct fop_method_record matcher = {.tag = name};
    struct fop_method_record *r =
        bsearch (&matcher, c->inner.tags_by_selector, c->inner.tags_len,
                 sizeof *c->inner.tags_by_selector, cmp_frm);
    if (r) {
        assert (selector == r->selector);
        assert (!strcmp (name, r->tag));
        assert (offset == r->offset);
        if (fn) {
            fop_vv_f *target = (((void *)c) + r->offset);
            *target = fn;
            r->method = fn;
        }
        return 0;
    }
    if (c->inner.tags_cap <= c->inner.tags_len) {
        size_t new_cap = (c->inner.tags_cap + 1) * 2;
        new_cap = new_cap > 10 ? new_cap : 10;
        c->inner.tags_by_selector =
            realloc (c->inner.tags_by_selector, sizeof *r * new_cap);
        assert (c->inner.tags_by_selector);
        c->inner.tags_cap = new_cap;
    }
    r = c->inner.tags_by_selector + c->inner.tags_len;
    assert (r);
    r->tag = name;
    r->offset = offset;
    r->selector = selector;
    r->method = fn;
    c->inner.tags_len++;
    qsort (c->inner.tags_by_selector, c->inner.tags_len,
           sizeof *c->inner.tags_by_selector, cmp_frm);
    return 0;
}
int fop_register_and_implement_methods (fop_class_t *c,
                                        struct fop_method_record *records)
{
    if (!c || !records)
        return -1;
    struct fop_method_record *cur = records;
    while (cur->selector) {
        fop_register_method (c, cur->selector, cur->tag, cur->offset,
                             cur->method);
        cur++;
    }
    return 0;
}

void *fop_initialize (void *self, va_list *app)
{
    if (!self)
        return NULL;
    const fop_class_t *c = fop_get_class (self);
    assert (c);
    return c->initialize ? c->initialize (self, app) : self;
};
void *fop_finalize (void *o)
{
    if (!o)
        return NULL;
    const fop_class_t *c = fop_get_class (o);
    assert (c);
    return c->finalize ? c->finalize (o) : o;
};
int fop_retain (void *o)
{
    if (!o)
        return -1;
    const fop_class_t *c = fop_get_class (o);
    assert (c);
    return c->retain ? c->retain (o) : -1;
};
int fop_release (void *o)
{
    if (!o)
        return -1;
    const fop_class_t *c = fop_get_class (o);
    assert (c);
    return c->release ? c->release (o) : -1;
};
int fop_describe (void *o, FILE *s)
{
    if (!o)
        return -1;
    const fop_class_t *c = fop_get_class (o);
    assert (c);
    if (!c->describe) {
        return fop_represent (o, s);
    }
    return c->describe (o, s);
};
int fop_represent (void *o, FILE *s)
{
    if (!o)
        return -1;
    const fop_class_t *c = fop_get_class (o);
    assert (c);
    return c->represent ? c->represent (o, s) : -1;
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
        free (buf);
    }
    return res;
};
void *object_finalize (void *o)
{
    if (!fop_cast_object (o)) {
        return NULL;
    }
    return o;
};
int object_retain (void *o)
{
    fop_object_t *obj = fop_cast_object (o);
    if (!obj) {
        return -1;
    }
    assert (obj->refcount < 1 << 30);
    obj->refcount++;
    return 0;
};
int object_release (void *o)
{
    fop_object_t *obj = fop_cast_object (o);
    if (!obj) {
        return -1;
    }
    assert (obj->refcount > 0);
    if (--obj->refcount == 0) {
        fprintf (stderr, "finalizing: ");
        fop_describe (o, stderr);
        fprintf (stderr, "\n");
        fop_finalize (o);
        free (o);
    }
    return 0;
};
int object_represent (void *o, FILE *s)
{
    fop_object_t *obj = fop_cast_object (o);
    if (!obj) {
        fprintf (s, "<unknown ptr@%p>", o);
    } else {
        const fop_class_t *c = fop_get_class (obj);
        fprintf (s, "<%s@%p>", c->name, obj);
    }
    return 0;
};

// Class methods

void *class_new (const fop_class_t *c, va_list *app)
{
    va_list local_ap;
    va_copy (local_ap, *app);
    const char *name = va_arg (local_ap, char *);
    const fop_class_t *super = va_arg (local_ap, fop_class_t *);
    size_t size = va_arg (local_ap, size_t);
    va_end (local_ap);
    fop_class_t *new_class = fop_alloc (c, 0);
    assert (new_class);
    fprintf (stderr, "new class of type %s ", c->name);
    return fop_initialize (new_class, app);
};
void *class_initialize (void *c_in, va_list *app)
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
        if (super->inner.tags_by_selector) {
            if (!super->inner.sorted) {
                qsort (super->inner.tags_by_selector, super->inner.tags_len,
                       sizeof *super->inner.tags_by_selector, cmp_frm);
            }
            struct fop_method_record *new_tags =
                malloc (sizeof *new_tags * super->inner.tags_len);
            assert (new_tags);
            memcpy (new_tags, super->inner.tags_by_selector,
                    sizeof *new_tags * super->inner.tags_len);
            c->inner.tags_by_selector = new_tags;
            c->inner.tags_len = super->inner.tags_len;
            c->inner.tags_cap = super->inner.tags_len;
        }
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
     sizeof (object_tags) / sizeof (struct fop_method_record), object_tags,
     0},  // Inner
    class_new,
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
