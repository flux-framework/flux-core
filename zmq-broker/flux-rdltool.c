/* flux-rdltool -- Test interface to Flux RDL C API */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "util/optparse.h"
#include "rdl.h"

struct prog_ctx {
    optparse_t p;
    const char *filename;
    char *cmd;
    char **args;
};

static void fatal (int code, char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    vfprintf (stderr, format, ap);
    va_end (ap);
    exit (code);
}

static int parse_cmdline (struct prog_ctx *ctx, int ac, char **av)
{
    int rc;
    struct optparse_option opts [] = {
        {
            .name =    "config-file",
            .key =     'f',
            .has_arg = 1,
            .arginfo = "FILE",
            .usage =   "Load RDL config from filename FILE",
        },
        OPTPARSE_TABLE_END,
    };

    ctx->p = optparse_create (av[0]);
    optparse_set (ctx->p, OPTPARSE_USAGE, "[OPTIONS] CMD [ARGS]...");
    optparse_add_option_table (ctx->p, opts);
    optparse_add_doc (ctx->p, "\nSupported CMDs include:", 1);
    optparse_add_doc (ctx->p, " resource URI\t Print resource at URI", 1);
    optparse_add_doc (ctx->p, " tree URI\t print hiearchy tree at URI", 1);
    optparse_add_doc (ctx->p, " aggregate URI\t aggregate hiearchy tree at URI", 1);

    if ((rc = optparse_parse_args (ctx->p, ac, av)) < 0)
        fatal (1, "Failed to parse cmdline options\n");

    if (!av[rc])
        fatal (1, "Missing command\n");

    ctx->cmd = strdup (av[rc]);
    ctx->args = av + rc + 1;

    if (optparse_getopt (ctx->p, "config-file", &ctx->filename) == 0)
        ctx->filename = NULL;
    return (0);
}

void prog_ctx_destroy (struct prog_ctx *ctx)
{
    optparse_destroy (ctx->p);
    if (ctx->cmd)
        free (ctx->cmd);
    free (ctx);
}

struct prog_ctx *prog_ctx_create (int ac, char **av)
{
    struct prog_ctx *ctx = malloc (sizeof (*ctx));
    if (ctx == NULL)
        fatal (1, "Out of memory");
    ctx->filename = NULL;
    ctx->cmd = NULL;
    parse_cmdline (ctx, ac, av);
    return (ctx);
}

void output_resource (struct prog_ctx *ctx, struct rdl *rdl, const char *uri)
{
    json_object *o;
    struct resource *r = rdl_resource_get (rdl, uri);
    if (r == NULL)
        fatal (1, "Failed to find resource `%s'\n", uri);

    o = rdl_resource_json (r);
    fprintf (stdout, "%s:\n%s\n", uri, json_object_to_json_string (o));
    json_object_put (o);
    rdl_resource_destroy (r);
    return;
}

void print_resource (struct prog_ctx *ctx, struct resource *r, int pad)
{
    struct resource *c;

    fprintf (stdout, "%*s/%s\n", pad, "", rdl_resource_name (r));

    rdl_resource_iterator_reset (r);
    while ((c = rdl_resource_next_child (r))) {
        print_resource (ctx, c, pad+1);
        rdl_resource_destroy (c);
    }
}

void output_tree (struct prog_ctx *ctx, struct rdl *rdl, const char *uri)
{
    struct resource *r = rdl_resource_get (rdl, uri);
    if (r == NULL)
        fatal (1, "Failed to find resource `%s'\n", uri);
    print_resource (ctx, r, 0);
    rdl_resource_destroy (r);
}

void aggregate (struct prog_ctx *ctx, struct rdl *rdl, const char *uri)
{
    json_object *o;
    struct resource *r = rdl_resource_get (rdl, uri);
    if (r == NULL)
        fatal (1, "Failed to find resource `%s'\n", uri);

    o = rdl_resource_aggregate_json (r);
    fprintf (stdout, "%s:\n%s\n", uri, json_object_to_json_string (o));
    json_object_put (o);
    rdl_resource_destroy (r);
    return;
}

int main (int ac, char **av)
{
    struct prog_ctx *ctx = prog_ctx_create (ac, av);
    struct rdllib *l = rdllib_open ();

    struct rdl *rdl = rdl_loadfile (l, ctx->filename);
    if (!rdl)
        fatal (1, "Failed to load config file: %s\n", ctx->filename);

    if (strcmp (ctx->cmd, "resource") == 0) {
        output_resource (ctx, rdl, ctx->args[0]);
    }
    else if (strcmp (ctx->cmd, "tree") == 0) {
        output_tree (ctx, rdl, ctx->args[0]);
    }
    else if (strcmp (ctx->cmd, "aggregate") == 0) {
        aggregate (ctx, rdl, ctx->args[0]);
    }
    else
        fatal (1, "Unknown command: %s\n", ctx->cmd);

    rdllib_close (l);
    prog_ctx_destroy (ctx);
    return (0);
}




/*
 * vi:ts=4 sw=4 expandtab
 */
