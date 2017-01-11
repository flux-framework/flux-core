#include "fop_protected.h"

#include "src/common/libtap/tap.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct geom;
// geom_class metaclass
struct geomClass {
    fop_class_t _;
    double (*area) (struct geom *);
    double (*perim) (struct geom *);
};
const fop_class_t *geom_class_c ()
{
    static fop_class_t *cls = NULL;
    if (!cls) {
        cls = fop_new (fop_class_c (), "geom_class_c", fop_class_c (),
                       sizeof (struct geomClass));
    }
    fprintf(stderr, "passed init %p\n", cls);
    return cls;
}

// abstract class geom, instance of metaclass geom_class
struct geom {
    fop_object_t _;
};
double geom_area (struct geom *self);
double geom_perim (struct geom *self);
fop_class_t *geom_c ()
{
    static fop_class_t *cls = NULL;
    if (!cls) {
        cls = fop_new (geom_class_c (), "geom_c", fop_object_c (),
                       sizeof (struct geom));
    }
    return cls;
}

// selectors
double geom_area (struct geom *o)
{
    const struct geomClass *c = (void *)fop_get_class_checked (o, geom_c());
    assert (c->area);
    return c->area (o);
}
double geom_perim (struct geom *o)
{
    const struct geomClass *c = (void *)fop_get_class_checked (o, geom_c());
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
    struct rect *self = fop_cast (rect_c (), _self);
    self->w = va_arg (*app, double);
    self->h = va_arg (*app, double);
    fprintf (stderr, "INITIALIZING RECT!\n");
    return self;
}
double rect_area (struct geom *_r)
{
    struct rect *r = fop_cast (rect_c(), _r);
    return r->w * r->h;
}

double rect_perim (struct geom *_r)
{
    struct rect *r = fop_cast (rect_c(), _r);
    return r->w * 2 + 2 * r->h;
}
const fop_class_t *rect_c ()
{
    static struct geomClass *cls = NULL;
    if (!cls) {
        cls = fop_new (geom_class_c (), "rect_c", geom_c (),
                       sizeof (struct rect));
        cls->_.initialize = rect_init;
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
    struct circle *c = fop_cast (circle_c(), _c);
    return c->r * c->r * 3.14159;
}

double circle_perim (struct geom *_c)
{
    struct circle *c = fop_cast (circle_c(), _c);
    return 3.14159 * c->r * 2;
}
const fop_class_t *circle_c ()
{
    static struct geomClass *cls = NULL;
    if (!cls) {
        cls = fop_new (geom_class_c (), "circle_c", geom_c (),
                       sizeof (struct circle));
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
    fop_describe (r, stderr);
    fop_describe (c, stderr);
    fop_release (r);
    fop_release (c);
    return 0;
}
