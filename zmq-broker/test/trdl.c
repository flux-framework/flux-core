
/* trdl.c - test RDL C API */

#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>

#include "log.h"
#include "rdl.h"

static void perr (void *ctx, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}

void print_resource (struct resource *r, int pad)
{
    struct resource *c;

    fprintf (stdout, "%*s/%s\n", pad, "", rdl_resource_name (r));

    rdl_resource_iterator_reset (r);
    while ((c = rdl_resource_next_child (r))) {
        print_resource (c, pad+1);
        rdl_resource_destroy (c);
    }
}


int main (int argc, char *argv[])
{
    struct rdllib *l;
    struct rdl *rdl1, *rdl2;
    struct rdl_accumulator *a;
    struct resource *r, *c;
    int64_t val;

    const char *filename = argv[1];

    log_init (basename (argv[0]));
    rdllib_set_default_errf (NULL, &perr);

    if (!(l = rdllib_open ()))
        err_exit ("rdllib_open");

    if (filename == NULL || *filename == '\0')
        filename = getenv ("TESTRDL_INPUT_FILE");

    if (!(rdl1 = rdl_loadfile (l, filename)))
        err_exit ("loadfile: %s", filename);

    if (!(rdl2 = rdl_copy (rdl1)))
        err_exit ("copy");

    r = rdl_resource_get (rdl1, "default");
    if (rdl_resource_set_int (r, "test-tag", 5959) < 0)
        exit (1);
    rdl_resource_get_int (r, "test-tag", &val);
    if (val != 5959)
        exit (1);

    rdl_resource_delete_tag (r, "test-tag");

    c = rdl_resource_next_child (r);

    a = rdl_accumulator_create (rdl1);
    if (rdl_accumulator_add (a, c) < 0)
        exit (1);

    rdl2 = rdl_accumulator_copy (a);

    print_resource (rdl_resource_get (rdl2, "default"), 0);

    rdllib_close (l);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
