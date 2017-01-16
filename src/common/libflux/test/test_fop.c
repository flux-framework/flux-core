#include "src/common/libflux/fop_dynamic.h"

#include "src/common/libtap/tap.h"

#include <assert.h>
#include <jansson.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Interface implementation test
struct jsonableInterface {
    fop_interface_t _;
    json_t *(*to_json) (fop *o);
};
const fop_class_t *jsonable_interface_c ();
json_t *jsonable_to_json (fop *o)
{
    const struct jsonableInterface *iface =
        fop_get_interface (o, jsonable_interface_c ());
    if (!iface || !iface->to_json)
        return NULL;
    return iface->to_json (o);
}
const fop_class_t *jsonable_interface_c ()
{
    static fop_class_t *cls = NULL;
    if (fop_class_needs_init (&cls)) {
        cls =
            fop_new_interface_class ("jsonable_interface_c", fop_interface_c (),
                                     sizeof (struct jsonableInterface));
    }
    return cls;
}

// impl for geom
json_t *geom_to_json (void *o)
{
    fprintf (stderr, "in to_json\n");
    return o;
}
// END Dynamic method resolution test

struct geom;
// geom_class metaclass
struct geomClass {
    fop_class_t _;
    double (*area) (struct geom *);
    double (*perim) (struct geom *);
    struct jsonableInterface jsonable;
};
const fop_class_t *geom_class_c ()
{
    static fop_class_t *cls = NULL;
    if (fop_class_needs_init (&cls) ) {
        cls = fop_new_metaclass ("geom_class_c", fop_class_c (),
                                 sizeof (struct geomClass));
    }
    return cls;
}

// abstract class geom, instance of metaclass geom_class
struct geom {
    fop_object_t _;
};
double geom_area (struct geom *self);
double geom_perim (struct geom *self);
const fop_class_t *geom_c ();
void *geom_init (void *_self, va_list *app)
{
    struct rect *self = fop_initialize_super (geom_c (), _self, app);
    self = fop_cast (geom_c (), self);
    if (!self)
        return NULL;
    fprintf (stderr, "INITIALIZING GEOM!\n");
    return self;
}
void geom_fini (void *_c)
{
    fop_finalize_super (geom_c (), _c);
    fprintf (stderr, "FINALIZING GEOM!\n");
}
const fop_class_t *geom_c ()
{
    static struct geomClass *cls = NULL;
    if (fop_class_needs_init (&cls)) {
        cls = fop_new_class (geom_class_c (), "geom_c", fop_object_c (),
                             sizeof (struct geom));
        fop_class_set_init (cls, geom_init);
        fop_class_set_fini (cls, geom_fini);
        cls->jsonable.to_json = geom_to_json;

        fop_implement_interface ((void *)cls, jsonable_interface_c (),
                                 offsetof (struct geomClass, jsonable));
    }
    return (void *)cls;
}

// selectors
double geom_area (struct geom *o)
{
    const struct geomClass *c = (void *)fop_get_class_checked (o, geom_c ());
    assert (c->area);
    return c->area (o);
}
double geom_perim (struct geom *o)
{
    const struct geomClass *c = (void *)fop_get_class_checked (o, geom_c ());
    assert (c->perim);
    return c->perim (o);
}

// class rect, instance of metaclass geom_class, child of geom
struct rect {
    struct geom _;
    double w, h;
};
const fop_class_t *rect_c ();
void *rect_init (void *_self, va_list *app)
{
    struct rect *self = fop_initialize_super (rect_c (), _self, app);
    self = fop_cast (rect_c (), self);
    if (!self)
        return NULL;
    self->w = va_arg (*app, double);
    self->h = va_arg (*app, double);
    fprintf (stderr, "INITIALIZING RECT!\n");
    return self;
}
double rect_area (struct geom *_r)
{
    struct rect *r = fop_cast (rect_c (), _r);
    return r->w * r->h;
}

double rect_perim (struct geom *_r)
{
    struct rect *r = fop_cast (rect_c (), _r);
    return r->w * 2 + 2 * r->h;
}
const fop_class_t *rect_c ()
{
    static struct geomClass *cls = NULL;
    if (fop_class_needs_init (&cls)) {
        cls = fop_new (geom_class_c (), "rect_c", geom_c (),
                       sizeof (struct rect));
        fop_class_set_init (cls, rect_init);
        cls->area = rect_area;
        cls->perim = rect_perim;
    }
    return &cls->_;
}

// class rect, instance of metaclass geom_class, child of geom
struct circle {
    struct geom _;
    double r;
};
const fop_class_t *circle_c ();
double circle_area (struct geom *_c)
{
    struct circle *c = fop_cast (circle_c (), _c);
    return c->r * c->r * 3.14159;
}

double circle_perim (struct geom *_c)
{
    struct circle *c = fop_cast (circle_c (), _c);
    return 3.14159 * c->r * 2;
}
void circle_fini (void *_c)
{
    fop_finalize_super (circle_c (), _c);
    fprintf (stderr, "FINALIZING CIRCLE!\n");
}
void *circle_desc (void *_c, FILE *s)
{
    struct circle *c = fop_cast (circle_c (), _c);
    if (!c)
        return NULL;
    fprintf (s, "<Circle: radius=%lf>", c->r);
    return c;
}
const fop_class_t *circle_c ()
{
    static struct geomClass *cls = NULL;
    if (fop_class_needs_init (&cls)) {
        cls = fop_new (geom_class_c (), "circle_c", geom_c (),
                       sizeof (struct circle));
        fop_class_set_fini (cls, circle_fini);
        fop_class_set_describe (cls, circle_desc);
        cls->perim = circle_perim;
        cls->area = circle_area;
    }
    return &cls->_;
}

void measure (struct geom *g)
{
    printf ("a %lf\n", geom_area (g));
    printf ("p %lf\n", geom_perim (g));
}

int main (int argc, char *argv[])
{
    plan (2);
    struct rect *r = fop_new (rect_c (), 3.0, 4.0);
    ok (r != NULL);
    struct circle *c = fop_new (circle_c ());
    ok (c != NULL);
    c->r = 5;
    measure (&r->_);
    measure (&c->_);
    jsonable_to_json (r);
    fop_describe (r, stderr);
    fprintf (stderr, "\n");
    fop_describe (c, stderr);
    fprintf (stderr, "\n");
    fop_release (r);
    fop_release (c);
    return 0;
}
