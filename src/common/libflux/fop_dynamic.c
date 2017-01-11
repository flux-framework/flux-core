#include "fop_dynamic.h"

#include <assert.h>
#include <search.h>
#include <stdlib.h>
#include <string.h>

// method record comparators
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

// method registration and implementation functions

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

void fop_dynamic_class_init (fop_class_t *c, const fop_class_t *super)
{
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
}
