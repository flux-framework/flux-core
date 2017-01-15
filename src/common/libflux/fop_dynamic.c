#include "fop_protected.h"

#include "fop_dynamic.h"

#include "src/common/libutil/sds.h"

#include <assert.h>
#include <search.h>
#include <stdlib.h>
#include <string.h>

typedef void (*fop_vv_f) (void);

// method record comparators

static int cmp_interface_pair (const void *key, const void *member)
{
    const struct iface_pair *k = key;
    const struct iface_pair *p = member;
    if (k->iface == p->iface)
        return 0;
    return fop_cast (k->iface, p->iface) ? 0 : 1;
}

const fop_class_t *fop_interface_c ()
{
    // TODO: make un-allocatable
    static fop_class_t *cls = NULL;
    if (!cls) {
        cls = fop_new (fop_class_c (), "interface_class", fop_class_c (),
                       sizeof (fop_class_t));
    }
    return (void *)cls;
}

static void add_interface (fop_class_t *c, struct iface_pair *p)
{
    if (!c->inner.interfaces) {
        c->inner.interfaces = (void *)sdsempty ();
    }

    c->inner.interfaces =
        (void *)sdscatlen ((void *)c->inner.interfaces, p, sizeof *p);
}

void fop_implement_interface (fop_class_t *c,
                              const fop_class_t *interface,
                              size_t offset)
{
    c = fop_cast (fop_class_c (), c);
    interface = fop_cast (fop_interface_c (), interface);
    assert (c && interface);

    // Turn embedded interface into a valid object
    fop_object_t *embedded_if = ((void *)c) + offset;
    fop_tag_object (embedded_if, interface);

    struct iface_pair p = {(fop_class_t *)interface, NULL, offset};
    add_interface (c, &p);
}

void fop_implement_dynamic_interface (fop_class_t *c,
                                      const fop_class_t *interface,
                                      fop_interface_t *impl)
{
    c = fop_cast (fop_class_c (), c);
    interface = fop_cast (fop_interface_c (), interface);
    assert (c && interface);

    struct iface_pair p = {(fop_class_t *)interface, impl, 0};
    add_interface (c, &p);
}

const void *fop_get_interface (const fop *o, const fop_class_t *interface)
{
    // TODO: Make interfaces un-allocatable, not really classes, just
    // interfaces
    const fop_class_t *c = fop_get_class (o);
    fop_describe ((void *)c, stderr);
    fop_describe ((void *)interface, stderr);

    interface = fop_cast (fop_class_c (), interface);
    if (!c || !interface || !c->inner.interfaces)
        return NULL;
    struct iface_pair p = {interface, 0};
    size_t nel = sdslen ((void *)c->inner.interfaces) / sizeof p;
    const struct iface_pair *ret =
        lfind (&p, c->inner.interfaces, &nel, sizeof p, cmp_interface_pair);
    if (!ret || !(ret->impl || ret->offset))
        return NULL;
    return ret->impl ? ret->impl : ((void *)c) + ret->offset;
}

void fop_dynamic_class_init (fop_class_t *c, const fop_class_t *super)
{
    if (super->inner.interfaces) {
        c->inner.interfaces = (void *)sdsdup ((void *)super->inner.interfaces);
    }
}
