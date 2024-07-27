/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <math.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/parse_size.h"
#include "ccan/str/str.h"

#include "optparse.h"
#include "getopt.h"
#include "getopt_int.h"

/******************************************************************************
 *  Datatypes:
 *****************************************************************************/

/*
 *  Option parser data structure: (optparse_t implementation)
 */
struct opt_parser {
    char *         program_name;
    char *         fullname;        /* full program name for subcommands    */
    char *         usage;

    opt_log_f      log_fn;

    opt_fatalerr_f fatalerr_fn;
    void *         fatalerr_handle;

    int            option_index;

    int            left_margin;     /* Size of --help output left margin    */
    int            option_width;    /* Width of --help output for optiion   */
    int            current_group;   /* Current option group number          */
    zlist_t *      option_list;     /* List of options for this program     */

    unsigned int   skip_subcmds:1;  /* Do not Print subcommands in --help   */
    unsigned int   no_options:1;    /* Skip option processing for subcmd    */
    unsigned int   hidden:1;        /* If subcmd, skip in --help output     */
    unsigned int   posixly_correct:1; /* Value of GNU getopt posixly correct*/
    unsigned int   sorted:1;        /* Options,subcmds are sorted in usage  */

    int            seq;             /* If subcommand, insertion sequence    */

    zhash_t *      dhash;           /* Hash of ancillary data               */

    optparse_t *   parent;          /* Pointer to parent optparse struct    */
    zhash_t *      subcommands;     /* Hash of sub-commands                 */
};


/*
 *  Local option information structure:
 */
struct option_info {
    struct optparse_option * p_opt;   /* Copy of program option structure  */
    zlist_t *               optargs;  /* If non-NULL, the option argument(s) */
    const char *            optarg;   /* Pointer to last element in optargs */
    int                     seq;      /* Sequence in which option was added  */

    unsigned int            found;    /* number of times we saw this option */

    unsigned int            isdoc:1;  /* 1 if this is a 'doc-only' option   */
    unsigned int            autosplit:1;  /* 1 if we auto-split values into */
                                          /* optargs */
    unsigned int            hidden:1; /* Skip option in --help output       */
};

/******************************************************************************
 *  static Functions:
 *****************************************************************************/

static void optparse_option_destroy (struct optparse_option *o)
{
    if (o) {
        free ((void *)o->name);
        free ((void *)o->arginfo);
        free ((void *)o->usage);
        free (o);
    }
}

static struct optparse_option *
optparse_option_dup (const struct optparse_option *src)
{
    struct optparse_option *o = calloc (1, sizeof (*o));
    if (o == NULL)
        return NULL;
    if ((src->name
        && !(o->name = strdup (src->name)))
        || (src->arginfo && !(o->arginfo = strdup (src->arginfo)))
        || (src->usage && !(o->usage = strdup (src->usage))))
        goto err;

    o->key = src->key;
    o->group = src->group;
    o->has_arg = src->has_arg;
    o->flags = src->flags;
    o->cb = src->cb;

    return (o);

err:
    optparse_option_destroy (o);
    return NULL;
}

static void option_info_destroy (struct option_info *c)
{
    optparse_option_destroy (c->p_opt);
    if (c->optargs)
        zlist_destroy (&c->optargs);
    c->optarg = NULL;
    free (c);
}

static struct option_info *option_info_create (const struct optparse_option *o)
{
    struct option_info *c = calloc (1, sizeof (*c));
    if (!c)
        return NULL;

    if (!(c->p_opt = optparse_option_dup (o)))
        goto err;
    if (!o->name)
        c->isdoc = 1;
    if (o->flags & OPTPARSE_OPT_AUTOSPLIT)
        c->autosplit = 1;
    if (o->flags & OPTPARSE_OPT_HIDDEN)
        c->hidden = 1;
    return (c);
err:
    option_info_destroy (c);
    return NULL;
}

/*
 *  Sort function for option_info structures.
 *   We sort first by group, then sort "doc" strings first in group
 *   then options alphabetically by key if alphanumeric, otherwise
 *   by option name.
 */
static int option_info_cmp (void *arg1, void *arg2)
{
    const struct option_info *x = arg1;
    const struct option_info *y = arg2;
    const struct optparse_option *o1 = x->p_opt;
    const struct optparse_option *o2 = y->p_opt;

    if (o1->group == o2->group) {
        if (x->isdoc && y->isdoc)
            return (0);
        else if (x->isdoc)
            return (-1);
        else if (y->isdoc)
            return (1);
        else if (streq (o1->name, "help"))
            return -1;
        else if (streq (o2->name, "help"))
            return 1;
        else if (isalnum (o1->key) && isalnum (o2->key))
            return (o1->key - o2->key);
        else
            return (strcmp (o1->name, o2->name));
    }
    return (o1->group - o2->group);
}

/* As above but preserve original sequence of options
 */
static int option_info_seq (void *arg1, void *arg2)
{
    const struct option_info *x = arg1;
    const struct option_info *y = arg2;
    const struct optparse_option *o1 = x->p_opt;
    const struct optparse_option *o2 = y->p_opt;

    if (o1->group == o2->group) {
        if (x->isdoc && y->isdoc)
            return (0);
        else if (x->isdoc)
            return (-1);
        else if (y->isdoc)
            return (1);
        else
            return x->seq - y->seq;
    }
    return (o1->group - o2->group);
}


/*
 *  Return option_info structure for option with [name]
 */
static struct option_info *find_option_info (optparse_t *p, const char *name)
{
    struct option_info *o;
    if (name == NULL)
        return NULL;

    o = zlist_first (p->option_list);
    while (o) {
        if (o->p_opt->name != NULL
            && streq (o->p_opt->name, name))
            return o;
        o = zlist_next (p->option_list);
    }
    return NULL;
}

static struct option_info *find_option_by_val (optparse_t *p, int val)
{
    struct option_info *o = zlist_first (p->option_list);
    while (o) {
        if (o->p_opt->key == val)
            return o;
        o = zlist_next (p->option_list);
    }
    return NULL;
}

/*   Remove the options in table [opts], up to but not including [end]
 *    If [end] is NULL, remove all options.
 */
static void option_table_remove (optparse_t *p,
                                 struct optparse_option const opts[],
                                 const struct optparse_option *end)
{
    const struct optparse_option *o = opts;
    while (o->usage && o != end)
        optparse_remove_option (p, (o++)->name);
    return;
}

static int optparse_set_usage (optparse_t *p, const char *usage)
{
    if (p->usage)
        free (p->usage);
    p->usage = strdup (usage);
    return (0);
}

static optparse_err_t optparse_set_log_fn (optparse_t *p, opt_log_f fn)
{
    p->log_fn = fn;
    return (0);
}

static optparse_err_t optparse_set_fatalerr_fn (optparse_t *p,
                                                opt_fatalerr_f fn)
{
    p->fatalerr_fn = fn;
    return (0);
}

static optparse_err_t optparse_set_fatalerr_handle (optparse_t *p,
                                                    void *handle)
{
    p->fatalerr_handle = handle;
    return (0);
}

static optparse_err_t optparse_set_option_cb (optparse_t *p,
                                              const char *name,
                                              optparse_cb_f fn)
{
    struct option_info *o;

    if (!name)
        return (OPTPARSE_BAD_ARG);

    if (!(o = find_option_info (p, name)))
        return (OPTPARSE_BAD_ARG);

    o->p_opt->cb = fn;
    return (0);
}

/*
 *  Generic function that prints a message to stderr. Default logging function.
 */
static int log_stderr (const char *fmt, ...)
{
    int rc = 0;
    va_list ap;
    va_start (ap, fmt);
    rc = vfprintf (stderr, fmt, ap);
    va_end (ap);
    return (rc);
}

/*
 * Default fatalerr function.
 */
static int fatal_exit (void *h, int exit_code)
{
    exit (exit_code);
    // NORETURN
}

static const char * optparse_fullname (optparse_t *p)
{
    if (!p->fullname) {
        char buf [1024];
        snprintf (buf,
                  sizeof (buf) - 1,
                  "%s%s%s",
                  p->parent ? optparse_fullname (p->parent) :"",
                  p->parent ? " " : "",
                  p->program_name);
        p->fullname = strdup (buf);
    }
    return (p->fullname);
}


static void optparse_vlog (optparse_t *p, const char *fmt, va_list ap)
{
    char buf [4096];
    int len = sizeof (buf);
    int n;

    /* Prefix all 'vlog' messages with full program name */
    n = snprintf (buf, len, "%s: ", optparse_fullname (p));
    if (n >= len || n < 0) {
        (*p->log_fn) ("optparse_vlog: fullname too big!\n");
        return;
    }
    len -= n;
    n = vsnprintf (buf+n, len, fmt, ap);
    if (n >= len || n < 0) {
        buf [len-2] = '+';
        buf [len-1] = '\0';
    }
    (*p->log_fn) (buf);
}

static int optparse_fatalerr (optparse_t *p, int code)
{
    return ((*p->fatalerr_fn) (p->fatalerr_handle, code));
}

static int optparse_fatalmsg (optparse_t *p, int code, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    optparse_vlog (p, fmt, ap);
    va_end (ap);
    return (optparse_fatalerr (p, code));
}

static int opt_init (struct option *opt, struct optparse_option *o)
{
    if (opt == NULL || o == NULL)
        return (-1);

    opt->flag = NULL;
    opt->name = o->name;
    opt->has_arg = o->has_arg;
    opt->val = o->key;
    return (0);
}


/******************************************************************************
 *  Usage output helper functions:
 *****************************************************************************/

/*
 *  Find next eligible place to split a line.
 */
static char *find_word_boundary (char *str, char *from, char **next)
{
    char *p = from;

    /*
     * Back up past any non-whitespace if we are pointing in
     *  the middle of a word.
     */
    while ((p != str) && !isspace ((int)*p))
        --p;

    /*
     * Next holds next word boundary
     */
    *next = p+1;

    /*
     * Now move back to the end of the previous word
     */
    while ((p != str) && isspace ((int)*p))
        --p;

    if (p == str) {
        *next = str;
        return (NULL);
    }

    return (p+1);
}


/*
 *  Return the next segment of buffer not to exceed width.
 */
static char * get_next_segment (char **from, int width, char *buf, int bufsiz)
{
    int len;
    char * seg = *from;
    char *p;

    if (**from == '\0')
        return (NULL);

    if ((len = strlen (*from)) <= width) {
        *from = *from + len;
        return (seg);
    }

    if (!(p = find_word_boundary (seg, *from + width, from))) {
        /*
         *      Need to break up a word. Use user-supplied buffer.
         */
        strncpy (buf, seg, width);
        buf [width - 1]  = '-';
        buf [width] = '\0';
        /*
         * Adjust from to character eaten by '-'
         *  And return pointer to buf.
         */
        *from = seg + width - 1;
        return (buf);
    }

    *p = '\0';

    return (seg);
}

static int get_term_columns ()
{
    char *val;
    int   cols = 80;
    if ((val = getenv ("COLUMNS"))) {
        char *p;
        long lval = strtol (val, &p, 10);
        if (p && (*p == '\0'))
            cols = (int) lval;
    }
    /*
     *  Check cols for ridiculous values:
     */
    if (cols >= 256 || cols <= 16)
        cols = 80;
    return (cols);
}

static void
optparse_doc_print (optparse_t *p, struct optparse_option *o, int columns)
{
    char seg [128];
    char buf [4096];
    char *s;
    char *q;

    strncpy (buf, o->usage, sizeof (buf) - 1);
    q = buf;

    while ((s = get_next_segment (&q, columns, seg, sizeof (seg))))
        (*p->log_fn) ("%s\n", s);

    return;
}

static void
optparse_option_print (optparse_t *p, struct optparse_option *o, int columns)
{
    int n;
    char *equals = "";
    char *arginfo = "";
    char *s, *q;
    char info [81];
    char seg [81];
    char buf [4096];

    int descsiz;

    int left_pad = p->left_margin;
    int width = p->option_width;

    if (o->arginfo) {
        equals = "=";
        arginfo = (char *) o->arginfo;
    }

    if (isalnum (o->key)) {
        n = snprintf (info, sizeof (info), "%*s-%c, --%s%s%s",
                left_pad, "", o->key, o->name, equals, arginfo);
    }
    else {
        n = snprintf (info, sizeof (info), "%*s--%s%s%s",
                left_pad+4, "", o->name, equals, arginfo);
    }

    if ((n < 0) || (n > columns))
        snprintf(info + columns - 2, n + 1, "+");

    /*
     *  Copy "usage" string to buffer as we might modify below
     */
    q = buf;
    strncpy (buf, o->usage, sizeof (buf) - 1);

    descsiz = columns - width;
    s = get_next_segment (&q, descsiz, seg, sizeof (seg));

    /*
     *  Print first line of usage output. If the total length of
     *   of the usage message overflows the width we have allowed
     *   for it, then split the help message onto the next line.
     */
    if (n < width)
        (*p->log_fn) ("%-*s%s\n", width, info, s);
    else
        (*p->log_fn) ("%s\n%*s%s\n", info, width, "", s);

    /*  Get remaining usage lines (line-wrapped)
     */
    while ((s = get_next_segment (&q, descsiz, seg, sizeof (seg))))
        (*p->log_fn) ("%*s%s\n", width, "", s);

    return;
}

static int optparse_print_options (optparse_t *p)
{
    int columns;
    struct option_info *o;

    if (!p || !p->option_list || !zlist_size (p->option_list))
        return (0);

    /*
     *  Sort option table per sorted flag
     */
    zlist_sort (p->option_list,
                p->sorted ? option_info_cmp : option_info_seq);

    columns = get_term_columns();
    o = zlist_first (p->option_list);
    while (o) {
        if (o->isdoc)
            optparse_doc_print (p, o->p_opt, columns);
        else if (!o->hidden)
            optparse_option_print (p, o->p_opt, columns);
        o = zlist_next (p->option_list);
    }

    return (0);
}

static int subcmd_name_cmp (void *s1, void *s2)
{
    const optparse_t *cmd1 = s1;
    const optparse_t *cmd2 = s2;
    return strcmp (cmd1->program_name, cmd2->program_name);
}

static int subcmd_seq_cmp (void *s1, void *s2)
{
    const optparse_t *cmd1 = s1;
    const optparse_t *cmd2 = s2;
    return cmd1->seq - cmd2->seq;
}

static zlist_t *subcmd_list_sorted (optparse_t *p)
{
    optparse_t *cmd = NULL;
    zlist_t *subcmds = zlist_new ();

    cmd = zhash_first (p->subcommands);
    while (cmd) {
        zlist_append (subcmds, cmd);
        cmd = zhash_next (p->subcommands);
    }

    zlist_sort (subcmds, p->sorted ? subcmd_name_cmp : subcmd_seq_cmp);

    return (subcmds);
}

/*
 *  Print top usage string for optparse object 'parent'.
 *  Returns number of lines printed, or 0 on error.
 */
static int print_usage_with_subcommands (optparse_t *parent)
{
    int lines = 0;
    opt_log_f fp = parent->log_fn;
    zlist_t *subcmds = NULL;
    optparse_t *p = NULL;
    int nsubcmds = zhash_size (parent->subcommands);
    /*
     *  With subcommands, only print usage line for parent command
     *   if parent->usage is set, otherwise only print usage line for
     *   each subcommand.
     *  If parent->usage is NULL, and there are no subcommands registered,
     *   then emit a default usage line.
     */
    if (parent->usage) {
        (*fp) ("Usage: %s %s\n", optparse_fullname (parent), parent->usage);
        lines++;
    }
    if (nsubcmds == 0 || parent->skip_subcmds) {
        if (!parent->usage)
            (*fp) ("Usage: %s [OPTIONS]...\n", optparse_fullname (parent));
        return (1);
    }

    if (!(subcmds = subcmd_list_sorted (parent)))
        return (-1);

    p = zlist_first (subcmds);
    while (p) {
        if (!p->hidden) {
            (*fp) ("%5s: %s %s\n",
                    ++lines > 1 ? "or" : "Usage",
                    optparse_fullname (p),
                    p->usage ? p->usage : "[OPTIONS]");
        }
        p = zlist_next (subcmds);
    }
    zlist_destroy (&subcmds);
    return (lines);
}

static int print_usage (optparse_t *p)
{
    print_usage_with_subcommands (p);
    return optparse_print_options (p);
}

static int display_help (optparse_t *p, struct optparse_option *o,
    const char *optarg)
{
    optparse_fatal_usage (p, 0, NULL);
    /* noreturn */
    return (0);
}

static void optparse_child_destroy (void *arg)
{
    optparse_t *p = arg;
    /* Already unlinked from parent -- avoid attempt to re-unlink
     *  by zeroing out parent pointer
     */
    p->parent = NULL;
    optparse_destroy (p);
}



/******************************************************************************
 *  API Functions:
 *****************************************************************************/

/*
 *  Destroy option parser [p]
 */
void optparse_destroy (optparse_t *p)
{
    if (p == NULL)
        return;

    /* Unlink from parent */
    if (p->parent && p->parent->subcommands) {
        zhash_delete (p->parent->subcommands, p->program_name);
        /*
         * zhash_delete() will call optparse_child_destroy(), which
         * calls optparse_destroy() again. Return now and allow the
         * rest of the optparse object to be freed on the next pass.
         * (optparse_child_destroy() set p->parent = NULL)
         *
         * Note: This code path only occurs if a subcommand optparse
         *  object is destroyed before its parent.
         */
        return;
    }

    zlist_destroy (&p->option_list);
    zhash_destroy (&p->dhash);
    zhash_destroy (&p->subcommands);
    free (p->program_name);
    free (p->fullname);
    free (p->usage);
    free (p);
}

/*
 *   Create option parser for program named [prog]
 */
optparse_t *optparse_create (const char *prog)
{
    struct optparse_option help = {
        .name = "help",
        .key  = 'h',
        .usage = "Display this message.",
        .cb    = (optparse_cb_f) display_help,
    };

    struct opt_parser *p = calloc (1, sizeof (*p));
    if (!p)
        return NULL;

    if (!(p->program_name = strdup (prog))) {
        free (p);
        return NULL;
    }
    p->option_list = zlist_new ();
    p->dhash = zhash_new ();
    p->subcommands = zhash_new ();
    if (!p->option_list || !p->dhash || !p->subcommands) {
        free (p);
        return NULL;
    }

    p->log_fn = &log_stderr;
    p->fatalerr_fn = &fatal_exit;
    p->fatalerr_handle = NULL;
    p->left_margin = 2;
    p->option_width = 25;
    p->option_index = -1;
    p->posixly_correct = 1;
    p->sorted = 0;

    /*
     *  Register -h, --help
     */
    if (optparse_add_option (p, &help) != OPTPARSE_SUCCESS) {
        fprintf (stderr,
                 "failed to register --help option: %s\n",
                 strerror (errno));
        optparse_destroy (p);
        return (NULL);
    }

    return (p);
}

optparse_t *optparse_add_subcommand (optparse_t *p,
                                     const char *name,
                                     optparse_subcmd_f cb)
{
    optparse_t *new;

    if (p == NULL || cb == NULL || name == NULL)
        return (NULL);

    new = optparse_create (name);
    if (new == NULL)
        return (NULL);
    new->seq = zhash_size (p->subcommands);
    zhash_update (p->subcommands, name, (void *) new);
    zhash_freefn (p->subcommands, name, optparse_child_destroy);
    zhash_update (new->dhash, "optparse::cb", cb);
    new->parent = p;
    new->log_fn = p->log_fn;
    new->fatalerr_fn = p->fatalerr_fn;
    new->fatalerr_handle = p->fatalerr_handle;
    new->left_margin = p->left_margin;
    new->option_width = p->option_width;
    return (new);
}

optparse_t *optparse_get_subcommand (optparse_t *p, const char *name)
{
    return (optparse_t *) zhash_lookup (p->subcommands, name);
}

optparse_t *optparse_get_parent (optparse_t *p)
{
    return (p->parent);
}

optparse_err_t optparse_reg_subcommand (optparse_t *p,
                                        const char *name,
                                        optparse_subcmd_f cb,
                                        const char *usage,
                                        const char *doc,
                                        int flags,
                                        struct optparse_option const opts[])
{
    optparse_err_t e;
    optparse_t *new;
    if (!p || !name || !cb)
        return OPTPARSE_BAD_ARG;

    new = optparse_add_subcommand (p, name, cb);
    if (new == NULL)
        return OPTPARSE_NOMEM;
    if ((usage && (e = optparse_set (new,
                                     OPTPARSE_USAGE,
                                     usage)) != OPTPARSE_SUCCESS)
        || (doc && (e = optparse_add_doc (new,
                                          doc,
                                          -1)) != OPTPARSE_SUCCESS)
        || (opts
            && (e = optparse_add_option_table (new,
                                               opts) != OPTPARSE_SUCCESS))) {
        optparse_destroy (new);
        return (e);
    }
    if (flags & OPTPARSE_SUBCMD_SKIP_OPTS)
        new->no_options = 1;
    if (flags & OPTPARSE_SUBCMD_HIDDEN)
        new->hidden = 1;
    return OPTPARSE_SUCCESS;
}

optparse_err_t optparse_reg_subcommands (optparse_t *p,
                                         struct optparse_subcommand cmds[])
{
    optparse_err_t e;
    struct optparse_subcommand *cmd = &cmds[0];
    while (cmd->name) {
        e = optparse_reg_subcommand (p,
                                     cmd->name,
                                     cmd->fn,
                                     cmd->usage,
                                     cmd->doc,
                                     cmd->flags,
                                     cmd->opts);
        if (e != OPTPARSE_SUCCESS)
            return (e);
        cmd++;
    }
    return (OPTPARSE_SUCCESS);
}

/*
 *  Search for option with [name] in parser [p] returns -1 if option
 *   was never registered with this parser, 0 if option was not used,
 *   and 1 if option was specified in args. Returns pointer to optargp
 *   if non-NULL.
 */
int optparse_getopt (optparse_t *p, const char *name, const char **optargp)
{
    struct option_info *c;
    if (optargp)
        *optargp = NULL;

    if (!(c = find_option_info (p, name)))
        return (-1);

    if (c->found) {
        if (c->optargs && optargp)
            *optargp = c->optarg;
        return (c->found);
    }
    return (0);
}

bool optparse_hasopt (optparse_t *p, const char *name)
{
    int n;
    if ((n = optparse_getopt (p, name, NULL)) < 0) {
        optparse_fatalmsg (p,
                           1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name,
                           name);
        return false;
    }
    return (n > 0);
}

int optparse_get_int (optparse_t *p, const char *name, int default_value)
{
    int n;
    long l;
    const char *s;
    char *endptr;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        optparse_fatalmsg (p,
                           1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name,
                           name);
        return -1;
    }
    if (n == 0)
        return default_value;
    if (s == NULL)
        return n;
    if (strlen (s) == 0)
        goto badarg;
    errno = 0;
    l = strtol (s, &endptr, 10);
    if (errno || *endptr != '\0' || l < INT_MIN || l > INT_MAX)
        goto badarg;
    return l;
badarg:
    optparse_fatalmsg (p,
                       1,
                       "%s: Option '%s' requires an integer argument\n",
                       p->program_name,
                       name);
    return -1;
}

double optparse_get_double (optparse_t *p,
                            const char *name,
                            double default_value)
{
    int n;
    double d;
    const char *s;
    char *endptr;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        optparse_fatalmsg (p,
                           1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name,
                           name);
        return -1;
    }
    if (n == 0)
        return default_value;
    if (s == NULL || strlen (s) == 0)
        goto badarg;
    errno = 0;
    d = strtod (s, &endptr);
    if (errno || *endptr != '\0')
        goto badarg;
    return d;
badarg:
    optparse_fatalmsg (p,
                       1,
                       "%s: Option '%s' requires a floating point argument\n",
                       p->program_name,
                       name);
    return -1;
}

double optparse_get_duration (optparse_t *p,
                              const char *name,
                              double default_value)
{
    int n;
    double d;
    const char *s = NULL;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        optparse_fatalmsg (p,
                           1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name,
                           name);
    }
    if (n == 0)
        return default_value;
    if (fsd_parse_duration (s, &d) < 0) {
        optparse_fatalmsg (p,
                           1,
                           "%s: Invalid argument for option '%s': '%s'\n",
                           p->program_name,
                           name,
                           s);
        return -1;
    }
    return d;
}

uint64_t optparse_get_size (optparse_t *p,
                            const char *name,
                            const char *default_value)
{
    int n;
    uint64_t result;
    const char *s = NULL;

    if (default_value == NULL)
        default_value = "0";

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        optparse_fatalmsg (p,
                           1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name,
                           name);
        return (uint64_t) -1;
    }
    if (n == 0)
        s = default_value;
    if (parse_size (s, &result) < 0) {
        optparse_fatalmsg (p,
                           1,
                          "%s: invalid argument for option '%s': %s: %s\n",
                          p->program_name,
                          name,
                          s,
                          strerror (errno));
        return (uint64_t) -1;
    }
    return result;
}

int optparse_get_size_int (optparse_t *p,
                           const char *name,
                           const char *default_value)
{
    uint64_t val = optparse_get_size (p, name, default_value);
    if (val == (uint64_t)-1)
        return -1;
    if (val > INT_MAX) {
        const char *s;
        optparse_getopt (p, name, &s);
        optparse_fatalmsg (p,
                           1,
                           "%s: %s: value %s too large (must be < %s)\n",
                           p->program_name,
                           name,
                           s,
                           encode_size (INT_MAX+1UL));
        return -1;
    }
    return (int)val;
}

const char *optparse_get_str (optparse_t *p,
                              const char *name,
                              const char *default_value)
{
    int n;
    const char *s;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        optparse_fatalmsg (p,
                           1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name,
                           name);
        return NULL;
    }
    if (n == 0)
        return default_value;
    return s;
}

const char *optparse_getopt_next (optparse_t *p, const char *name)
{
    struct option_info *c;
    const char *current;
    if (!(c = find_option_info (p, name)))
        return (NULL);
    if (c->found == 0 || !c->optargs)
        return (NULL);
    /*  Return current item and advance list */
    if (!(current = zlist_item (c->optargs)))
        return NULL;
    zlist_next (c->optargs);
    return current;
}

int optparse_getopt_iterator_reset (optparse_t *p, const char *name)
{
    struct option_info *c;
    if (!(c = find_option_info (p, name)))
        return (-1);
    if (c->found == 0 || !c->optargs)
        return (0);
    zlist_first (c->optargs);
    return (zlist_size (c->optargs));
}

optparse_err_t optparse_add_option (optparse_t *p,
                                    const struct optparse_option *o)
{
    struct option_info *c;

    if (p == NULL || !p->option_list)
        return OPTPARSE_BAD_ARG;

    if (o->name && find_option_info (p, o->name))
        return OPTPARSE_EEXIST;

    if (!(c = option_info_create (o)))
        return OPTPARSE_NOMEM;

    c->seq = zlist_size (p->option_list);

    if (zlist_append (p->option_list, c) < 0)
        return OPTPARSE_NOMEM;
    zlist_freefn (p->option_list,
                  c,
                  (zlist_free_fn *) option_info_destroy,
                  true);

    return (OPTPARSE_SUCCESS);
}

optparse_err_t optparse_remove_option (optparse_t *p, const char *name)
{
    struct option_info *o = NULL;

    if (!p || !name)
        return OPTPARSE_FAILURE;

    if (!(o = find_option_info (p, name)))
        return OPTPARSE_FAILURE;
    zlist_remove (p->option_list, o);

    return OPTPARSE_SUCCESS;
}

optparse_err_t optparse_add_option_table (optparse_t *p,
                                          struct optparse_option const opts[])
{
    optparse_err_t rc = OPTPARSE_SUCCESS;
    const struct optparse_option *o = opts;

    if (!p)
        return OPTPARSE_BAD_ARG;

    while (o->usage) {
        if ((rc = optparse_add_option (p, o++)) != OPTPARSE_SUCCESS) {
            option_table_remove (p, opts, o-1);
            return (rc);
        }
    }
    return (rc);
}

optparse_err_t optparse_add_doc (optparse_t *p, const char *doc, int group)
{
    struct optparse_option o;

    if (p == NULL || !p->option_list)
        return OPTPARSE_BAD_ARG;

    memset (&o, 0, sizeof (o));
    o.usage = doc;
    o.group = group;
    return optparse_add_option (p, &o);
}

optparse_err_t optparse_set (optparse_t *p, int item, ...)
{
    optparse_err_t e = OPTPARSE_SUCCESS;
    va_list vargs;
    optparse_cb_f cb;
    char *str;
    int n;

    if (p == NULL)
        return OPTPARSE_BAD_ARG;

    va_start (vargs, item);
    switch (item) {
    case OPTPARSE_USAGE:
        e = optparse_set_usage (p, va_arg (vargs, char *));
        break;
    case OPTPARSE_LOG_FN:
        e = optparse_set_log_fn (p, va_arg (vargs, void *));
        break;
    case OPTPARSE_FATALERR_FN:
        e = optparse_set_fatalerr_fn (p, va_arg (vargs, void *));
        break;
    case OPTPARSE_FATALERR_HANDLE:
        e = optparse_set_fatalerr_handle (p, va_arg (vargs, void *));
        break;
    case OPTPARSE_LEFT_MARGIN:
        n = va_arg (vargs, int);
        if ((n < 0) || (n > 1000))
            e = OPTPARSE_BAD_ARG;
        else
            p->left_margin = n;
        break;
    case OPTPARSE_OPTION_CB:
        str = va_arg (vargs, char *);
        cb = va_arg (vargs, void *);
        e = optparse_set_option_cb (p, str, cb);
        break;
    case OPTPARSE_OPTION_WIDTH:
        n = va_arg (vargs, int);
        if ((n < 0) || (n > 1000))
            e = OPTPARSE_BAD_ARG;
        else
            p->option_width = n;
        break;
    case OPTPARSE_PRINT_SUBCMDS:
        n = va_arg (vargs, int);
        p->skip_subcmds = !n;
        break;
    case OPTPARSE_SUBCMD_NOOPTS:
        n = va_arg (vargs, int);
        p->no_options = n;
        break;
    case OPTPARSE_SUBCMD_HIDE:
        n = va_arg (vargs, int);
        p->hidden = n;
        break;
    case OPTPARSE_POSIXLY_CORRECT:
        n = va_arg (vargs, int);
        p->posixly_correct = n;
        break;
    case OPTPARSE_SORTED:
        n = va_arg (vargs, int);
        p->sorted = n;
        break;
    default:
        e = OPTPARSE_BAD_ARG;
    }
    va_end (vargs);
    return e;
}

static void * lookup_recursive (optparse_t *p, const char *key)
{
    void *d;
    do {
        if ((d = zhash_lookup (p->dhash, key)))
            return (d);
        p = p->parent;
    } while (p);
    return (NULL);
}


optparse_err_t optparse_get (optparse_t *p, int item, ...)
{
    optparse_err_t e = OPTPARSE_SUCCESS;
    va_list vargs;

    if (p == NULL)
        return OPTPARSE_BAD_ARG;

    va_start (vargs, item);
    switch (item) {
    /*
     *  Not implemented yet...
     */
    case OPTPARSE_USAGE:
    case OPTPARSE_LOG_FN:
    case OPTPARSE_FATALERR_FN:
    case OPTPARSE_FATALERR_HANDLE:
    case OPTPARSE_LEFT_MARGIN:
    case OPTPARSE_OPTION_CB:
    case OPTPARSE_OPTION_WIDTH:
    case OPTPARSE_PRINT_SUBCMDS:
    case OPTPARSE_SUBCMD_NOOPTS:
    case OPTPARSE_SUBCMD_HIDE:
    case OPTPARSE_POSIXLY_CORRECT:
    case OPTPARSE_SORTED:
        e = OPTPARSE_NOT_IMPL;
        break;

    default:
        e = OPTPARSE_BAD_ARG;
    }
    va_end (vargs);
    return e;
}

void optparse_set_data (optparse_t *p, const char *s, void *x)
{
    zhash_insert (p->dhash, s, x);
}

void *optparse_get_data (optparse_t *p, const char *s)
{
    return lookup_recursive (p, s);
}

static char * optstring_create ()
{
    char *optstring = calloc (4, 1);
    if (optstring == NULL)
        return (NULL);
    /* Per getopt(3) manpage
     *
     * "If the first character ... of optstring is a colon (':'), then
     * getopt() returns ':' instead of '?' to indicate a missing
     * option argument"
     */
    optstring[0] = ':';
    optstring[1] = '\0';
    return (optstring);
}

static char * optstring_append (char *optstring, struct optparse_option *o)
{
    int n, len;
    char *colons = "";

    if (!isalnum (o->key))
        return (optstring);

    if (!optstring && !(optstring = optstring_create ()))
        return (NULL);

    /*
     *  We need to add a single character to optstring for an
     *   an option with no argument (has_arg = 0), 2 characters
     *   for a required argument (has_arg = 1), and 3 chars for
     *   an optional argument "o::" (has_arg = 2)
     *  N.B.: optional argument is applied to short options only if
     *   OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG is set on flags
     */
    len = strlen (optstring);
    n = len + o->has_arg + 1;

    optstring = realloc (optstring, (n + 1)); /* extra character for NUL */

    if (o->has_arg == 1)
        colons = ":";
    else if (o->has_arg == 2 && o->flags & OPTPARSE_OPT_SHORTOPT_OPTIONAL_ARG)
        colons = "::";

    sprintf (optstring+len, "%c%s", o->key, colons);

    return (optstring);
}

/*
 *  Create getopt_long(3) option table and return.
 *  Also create shortopts table if sp != NULL and return by value.
 */
static struct option * option_table_create (optparse_t *p, char **sp)
{
    struct option_info *o;
    struct option *opts;
    int n;
    int j;

    n = zlist_size (p->option_list);
    opts = calloc ((n + 1), sizeof (struct option));
    if (opts == NULL)
        return (NULL);

    if (sp) {
        *sp = optstring_create ();
        if ((*sp) == NULL) {
            free (opts);
            return (NULL);
        }
    }

    j = 0;
    o = zlist_first (p->option_list);
    while (o) {
        if (!o->isdoc) {
            /* Initialize option field from cached option structure */
            opt_init (&opts[j++], o->p_opt);
            if (sp) {
                *sp = optstring_append (*sp, o->p_opt);
                if (*sp == NULL) {
                    free (opts);
                    opts = NULL;
                    break;
                }
            }
        }
        o = zlist_next (p->option_list);
    }

    /*
     *  Initialize final element of option array to zeros:
     */
    if (opts)
        memset (&opts[j], 0, sizeof (struct option));

    return (opts);
}

static int opt_append_sep (struct option_info *opt, const char *str)
{
    error_t e;
    char *arg = NULL;
    char *argz = NULL;
    size_t len = 0;
    if ((e = argz_create_sep (str, ',', &argz, &len))) {
        errno = e;
        return (-1);
    }
    while ((arg = argz_next (argz, len, arg))) {
        char *s = strdup (arg);
        if (s == NULL)
            return -1;
        opt->optarg = s;
        zlist_append (opt->optargs, s);
        zlist_freefn (opt->optargs, s, free, true);
    }
    zlist_first (opt->optargs);
    free (argz);
    return (0);
}

static void opt_append_optarg (optparse_t *p,
                               struct option_info *opt,
                               const char *optarg)
{
    char *s;
    if (!opt->optargs)
        opt->optargs = zlist_new ();
    if (opt->autosplit) {
        if (opt_append_sep (opt, optarg) < 0)
            optparse_fatalmsg (p,
                               1,
                               "%s: append '%s': %s\n",
                               p->program_name,
                               optarg,
                               strerror (errno));
        return;
    }
    if ((s = strdup (optarg)) == NULL)
        optparse_fatalmsg (p, 1, "%s: out of memory\n", p->program_name);
    zlist_append (opt->optargs, s);
    zlist_freefn (opt->optargs, s, free, true);
    opt->optarg = s;
    /*  Ensure iterator is reset on first append */
    zlist_first (opt->optargs);
}

/*
 *  Call reentrant internal version of getopt_long() directly copied from
 *   glibc. See getopt.c and getopt_int.h in this directory.
 */
static int getopt_long_r (int argc,
                          char *const *argv,
                          const char *options,
                          const struct option *long_options,
                          int *opt_index,
                          struct _getopt_data *d,
                          int posixly_correct)
{
  return _getopt_internal_r (argc,
                             argv,
                             options,
                             long_options,
                             opt_index,
                             0,
                             d,
                             posixly_correct);
}

int optparse_parse_args (optparse_t *p, int argc, char *argv[])
{
    int c;
    int li = -1;
    struct _getopt_data d;
    const char *fullname = NULL;
    char *optstring = NULL;
    struct option *optz = option_table_create (p, &optstring);

    if (!optz)
        return -1;

    fullname = optparse_fullname (p);

    /* Initialize getopt internal state data:
     */
    memset (&d, 0, sizeof (d));

    while ((c = getopt_long_r (argc,
                               argv,
                               optstring,
                               optz,
                               &li,
                               &d,
                               p->posixly_correct)) >= 0) {
        struct option_info *opt;
        struct optparse_option *o;
        if (c == ':') {
            (*p->log_fn) ("%s: '%s' missing argument\n",
                          fullname,
                          argv[d.optind-1]);
            d.optind = -1;
            break;
        }
        else if (c == '?') {
            if (d.optopt != '\0')
                (*p->log_fn) ("%s: unrecognized option '-%c'\n",
                              fullname,
                              d.optopt);
            else
                (*p->log_fn) ("%s: unrecognized option '%s'\n",
                              fullname,
                              argv[d.optind-1]);
            (*p->log_fn) ("Try `%s --help' for more information.\n",
                          fullname);
            d.optind = -1;
            break;
        }

        /* If li is a valid index, then a long option was used and we can
         *  use this index into optz to get corresponding option name,
         *  otherwise, assume a short option and use returned character
         *  to find option by its key/value.
         */
        if (li >= 0)
            opt = find_option_info (p, optz[li].name);
        else
            opt = find_option_by_val (p, c);

        /* Reset li for next iteration */
        li = -1;
        if (opt == NULL) {
            (*p->log_fn) ("ugh, didn't find option associated with char %c\n",
                          c);
            continue;
        }

        opt->found++;
        if (d.optarg)
            opt_append_optarg (p, opt, d.optarg);

        o = opt->p_opt;
        if (o->cb && ((o->cb) (p, o, d.optarg) < 0)) {
            (*p->log_fn) ("Option \"%s\" failed\n", o->name);
            d.optind = -1;
            break;
        }
    }

    free (optz);
    free (optstring);
    p->option_index = d.optind;
    return (p->option_index);
}

int optparse_run_subcommand (optparse_t *p, int argc, char *argv[])
{
    int ac;
    char **av;
    optparse_subcmd_f cb;
    optparse_t *sp;

    if (p->option_index == -1) {
        if (optparse_parse_args (p, argc, argv) < 0)
            return optparse_fatalerr (p, 1);
    }

    ac = argc - p->option_index;
    av = argv + p->option_index;

    if (ac <= 0)
        return optparse_fatal_usage (p, 1, "missing subcommand\n");

    if (!(sp = zhash_lookup (p->subcommands, av[0]))) {
        return optparse_fatal_usage (p, 1, "Unknown subcommand: %s\n", av[0]);
    }

    if (!sp->no_options && (optparse_parse_args (sp, ac, av) < 0))
        return optparse_fatalerr (sp, 1);

    if (!(cb = zhash_lookup (sp->dhash, "optparse::cb"))) {
        return optparse_fatalmsg (p,
                                  1,
                                  "subcommand %s: failed to lookup callback!\n",
                                  av[0]);
    }

    return ((*cb) (sp, ac, av));
}

int optparse_print_usage (optparse_t *p)
{
    return print_usage (p);
}

int optparse_fatal_usage (optparse_t *p, int code, const char *fmt, ...)
{
    if (fmt) {
        va_list ap;
        va_start (ap, fmt);
        optparse_vlog (p, fmt, ap);
        va_end (ap);
    }
    print_usage (p);
    return (*p->fatalerr_fn) (p->fatalerr_handle, code);
}

int optparse_option_index (optparse_t *p)
{
    return (p->option_index);
}

/*
 *  Reset one optparse_t object -- set option_index to zero and
 *   reset and free option arguments, etc.
 */
static void optparse_reset_one (optparse_t *p)
{
    struct option_info *o;

    if (!p)
        return;

    p->option_index = -1;

    if (!p->option_list || !zlist_size (p->option_list))
        return;

    o = zlist_first (p->option_list);
    while (o) {
        if (o->isdoc)
            continue;
        o->found = 0;
        if (o->optargs)
            zlist_destroy (&o->optargs);
        o->optarg = NULL;
        o = zlist_next (p->option_list);
    }
    return;
}

void optparse_reset (optparse_t *p)
{
    optparse_t *cmd = zhash_first (p->subcommands);
    while (cmd) {
        optparse_reset_one (cmd);
        cmd = zhash_next (p->subcommands);
    }
    optparse_reset_one (p);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
