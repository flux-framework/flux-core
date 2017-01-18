#include "fop_protected.h"

#include "fop_dynamic.h"

#include "src/common/libutil/sds.h"

#include <assert.h>
#include <search.h>
#include <stdlib.h>
#include <string.h>

const fop_class_t *fop_interface_c ()
{
    // TODO: make un-allocatable
    static fop_class_t *cls = NULL;
    if (fop_class_needs_init (&cls)) {
        cls = fop_new_metaclass ("interface_class", fop_class_c (),
                                 sizeof (fop_class_t));
    }
    return (void *)cls;
}

fop_class_t *fop_new_interface_class (const char *name,
                                      const fop_class_t *parent,
                                      size_t size)
{
    return fop_new (fop_interface_c (), name, parent, size);
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

const void *fop_get_interface (const fop *o, const fop_class_t *interface)
{
    // TODO: Make interfaces un-allocatable, not really classes, just
    // interfaces
    const fop_class_t *c = fop_get_class (o);

    interface = fop_cast (fop_interface_c (), interface);
    if (!c || !interface || !c->inner.interfaces)
        return NULL;
    const struct iface_pair *ret = NULL;
    for (size_t i = 0; i < sdslen((void *)c->inner.interfaces); ++i) {
        const struct iface_pair *cur = &c->inner.interfaces[i];
        if (interface == cur->iface || fop_cast (interface, cur->iface)) {
            ret = cur;
            break;
        }
    }
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
