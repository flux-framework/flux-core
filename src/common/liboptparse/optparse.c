/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
#include <argz.h>

#include <czmq.h>

#include "src/common/liblsd/list.h"
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

    int            optind;

    int            left_margin;     /* Size of --help output left margin    */
    int            option_width;    /* Width of --help output for optiion   */
    int            current_group;   /* Current option group number          */
    List           option_list;     /* List of options for this program    */

    unsigned int   skip_subcmds:1;  /* Do not Print subcommands in --help   */
    unsigned int   no_options:1;    /* Skip option processing for subcmd    */

    zhash_t *      dhash;           /* Hash of ancillary data               */

    optparse_t *   parent;          /* Pointer to parent optparse struct    */
    zhash_t *      subcommands;     /* Hash of sub-commands                 */
};


/*
 *  Local option information structure:
 */
struct option_info {
    struct optparse_option * p_opt;   /* Copy of program option structure  */
    List                    optargs;  /* If non-NULL, the option argument(s) */
    const char *            optarg;   /* Pointer to last element in optargs  */
    ListIterator            argi;     /* iterator for optargs */

    unsigned int            found;    /* number of times we saw this option */

    unsigned int            isdoc:1;  /* 1 if this is a 'doc-only' option   */
    unsigned int            autosplit:1;  /* 1 if we auto-split values into */
                                          /* optargs */
};

/******************************************************************************
 *  static Functions:
 *****************************************************************************/

static void optparse_option_destroy (struct optparse_option *o)
{
    if (o == NULL)
        return;
    free ((void *)o->name);
    free ((void *)o->arginfo);
    free ((void *)o->usage);
    free (o);
}

static struct optparse_option *
optparse_option_dup (const struct optparse_option *src)
{
    struct optparse_option *o = malloc (sizeof (*o));
    if (o != NULL) {
        memset (o, 0, sizeof (*o));
        if (src->name)
            o->name =    strdup (src->name);
        if (src->arginfo)
            o->arginfo = strdup (src->arginfo);
        if (src->usage)
            o->usage =   strdup (src->usage);
        o->key =     src->key;
        o->group =   src->group;
        o->has_arg = src->has_arg;
        o->flags =   src->flags;
        o->cb  =     src->cb;
    }
    return (o);
}

static struct option_info *option_info_create (const struct optparse_option *o)
{
    struct option_info *c = malloc (sizeof (*c));
    if (c != NULL) {
        memset (c, 0, sizeof (*c));
        c->found = 0;
        c->optargs = NULL;
        c->optarg = NULL;
        c->argi = NULL;
        c->p_opt = optparse_option_dup (o);
        if (!o->name)
            c->isdoc = 1;
        if (o->has_arg == 3)
            c->autosplit = 1;
    }
    return (c);
}

static void option_info_destroy (struct option_info *c)
{
    optparse_option_destroy (c->p_opt);
    if (c->argi)
        list_iterator_destroy (c->argi);
    if (c->optargs)
        list_destroy (c->optargs);
    c->optarg = NULL;
    free (c);
}

/*
 *  Sort function for option_info structures.
 *   We sort first by group, then sort "doc" strings first in group
 *   then options alphabetically by key if alphanumeric, otherwise
 *   by option name.
 */
static int option_info_cmp (struct option_info *x, struct option_info *y)
{
    const struct optparse_option *o1 = x->p_opt;
    const struct optparse_option *o2 = y->p_opt;

    if (o1->group == o2->group) {
        if (x->isdoc && y->isdoc)
            return (0);
        else if (x->isdoc)
            return (-1);
        else if (y->isdoc)
            return (1);
        else if (isalnum (o1->key) && isalnum (o2->key))
            return (o1->key - o2->key);
        else
            return (strcmp (o1->name, o2->name));
    }
    return (o1->group - o2->group);
}


/*
 *  List find function for option_info structures:
 */
static int option_info_cmp_name (struct option_info *o, const char *name)
{
    if (!o->p_opt->name)
        return (0);
    return (strcmp (o->p_opt->name, name) == 0);
}

/*
 *  Return option_info structure for option with [name]
 */
static struct option_info *find_option_info (optparse_t *p, const char *name)
{
    return (list_find_first (p->option_list,
                             (ListFindF) option_info_cmp_name,
                             (void *) name));
}

/*   Remove the options in table [opts], up to but not including [end]
 *    If [end] is NULL, remove all options.
 */
static void option_table_remove (optparse_t *p,
        struct optparse_option const opts[],
        const struct optparse_option *end)
{
    const struct optparse_option *o = opts;

    while (o->usage || (end && o != end))
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

static optparse_err_t optparse_set_fatalerr_fn (optparse_t *p, opt_fatalerr_f fn)
{
    p->fatalerr_fn = fn;
    return (0);
}

static optparse_err_t optparse_set_fatalerr_handle (optparse_t *p, void *handle)
{
    p->fatalerr_handle = handle;
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
 * Default fatalerr funciton.
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
        snprintf (buf, sizeof (buf) - 1, "%s%s%s",
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
static char *find_word_boundary(char *str, char *from, char **next)
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
    ListIterator i;

    if (!p || !p->option_list || !list_count(p->option_list))
        return (0);

    /*
     *  Sort option table by group then by name
     */
    list_sort (p->option_list, (ListCmpF) option_info_cmp);

    columns = get_term_columns();
    i = list_iterator_create (p->option_list);
    while ((o = list_next (i))) {
        if (o->isdoc)
            optparse_doc_print (p, o->p_opt, columns);
        else
            optparse_option_print (p, o->p_opt, columns);
    }
    list_iterator_destroy (i);

    return (0);
}

#if CZMQ_VERSION < CZMQ_MAKE_VERSION(3,0,1)
static bool s_cmp (const char *s1, const char *s2)
{
    return (strcmp (s1, s2) > 0);
}
#else
static int s_cmp (const char *s1, const char *s2)
{
    return strcmp (s1, s2);
}
#endif

static zlist_t *subcmd_list_sorted (optparse_t *p)
{
    zlist_t *keys = zhash_keys (p->subcommands);
    if (keys)
        zlist_sort (keys, (zlist_compare_fn *) s_cmp);
    return (keys);
}

/*
 *  Print top usage string for optparse object 'parent'.
 *  Returns number of lines printed, or 0 on error.
 */
static int print_usage_with_subcommands (optparse_t *parent)
{
    int lines = 0;
    const char *cmd;
    opt_log_f fp = parent->log_fn;
    zlist_t *keys;
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

    if (!(keys = subcmd_list_sorted (parent)))
        return (-1);

    cmd = zlist_first (keys);
    while (cmd) {
        optparse_t *p = zhash_lookup (parent->subcommands, cmd);;
        (*fp) ("%5s: %s %s\n",
               ++lines > 1 ? "or" : "Usage",
               optparse_fullname (p),
               p->usage ? p->usage : "[OPTIONS]");
        cmd = zlist_next (keys);
    }
    zlist_destroy (&keys);
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
    if (p->parent && p->parent->subcommands)
        zhash_delete (p->parent->subcommands, p->program_name);

    list_destroy (p->option_list);
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

    struct opt_parser *p = malloc (sizeof (*p));
    if (!p)
        return NULL;

    memset (p, 0, sizeof (*p));

    if (!(p->program_name = strdup (prog))) {
        free (p);
        return NULL;
    }
    p->usage = NULL;
    p->parent = NULL;
    p->option_list = list_create ((ListDelF) option_info_destroy);
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
    p->optind = -1;

    /*
     *  Register -h, --help
     */
    if (optparse_add_option (p, &help) != OPTPARSE_SUCCESS) {
        fprintf (stderr, "failed to register --help option: %s\n", strerror (errno));
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
                                        struct optparse_option const opts[])
{
    optparse_err_t e;
    optparse_t *new;
    if (!p || !name || !cb)
        return OPTPARSE_BAD_ARG;

    new = optparse_add_subcommand (p, name, cb);
    if (new == NULL)
        return OPTPARSE_NOMEM;
    if ((usage &&
         (e = optparse_set (new, OPTPARSE_USAGE, usage)) != OPTPARSE_SUCCESS)
     || (doc &&
         (e = optparse_add_doc (new, doc, -1)) != OPTPARSE_SUCCESS)
     || (opts &&
         (e = optparse_add_option_table (new, opts) != OPTPARSE_SUCCESS))) {
        optparse_destroy (new);
        return (e);
    }
    return OPTPARSE_SUCCESS;
}

optparse_err_t optparse_reg_subcommands (optparse_t *p,
                                         struct optparse_subcommand cmds[])
{
    optparse_err_t e;
    struct optparse_subcommand *cmd = &cmds[0];
    while (cmd->name) {
        e = optparse_reg_subcommand (p, cmd->name, cmd->fn, cmd->usage,
                                        cmd->doc, cmd->opts);
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
        optparse_fatalmsg (p, 1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name, name);
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
        optparse_fatalmsg (p, 1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name, name);
        return -1;
    }
    if (n == 0)
        return default_value;
    if (s == NULL || strlen (s) == 0)
        goto badarg;
    errno = 0;
    l = strtol (s, &endptr, 10);
    if (errno || *endptr != '\0' || l < INT_MIN || l > INT_MAX)
        goto badarg;
    return l;
badarg:
    optparse_fatalmsg (p, 1,
                       "%s: Option '%s' requires an integer argument\n",
                       p->program_name, name);
    return -1;
}

double optparse_get_double (optparse_t *p, const char *name,
                            double default_value)
{
    int n;
    double d;
    const char *s;
    char *endptr;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        optparse_fatalmsg (p, 1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name, name);
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
    optparse_fatalmsg (p, 1,
                       "%s: Option '%s' requires a floating point argument\n",
                       p->program_name, name);
    return -1;
}

const char *optparse_get_str (optparse_t *p, const char *name,
                              const char *default_value)
{
    int n;
    const char *s;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        optparse_fatalmsg (p, 1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name, name);
        return NULL;
    }
    if (n == 0)
        return default_value;
    return s;
}

const char *optparse_getopt_next (optparse_t *p, const char *name)
{
    struct option_info *c;
    if (!(c = find_option_info (p, name)))
        return (NULL);
    if (c->found == 0 || !c->optargs)
        return (NULL);
    if (!c->argi)
        c->argi = list_iterator_create (c->optargs);
    return (list_next (c->argi));
}

int optparse_getopt_iterator_reset (optparse_t *p, const char *name)
{
    struct option_info *c;
    if (!(c = find_option_info (p, name)))
        return (-1);
    if (c->found == 0 || !c->optargs)
        return (0);
    if (c->argi)
        list_iterator_reset (c->argi);
    return (list_count (c->optargs));
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

    if (!list_append (p->option_list, c))
        return OPTPARSE_NOMEM;

    return (OPTPARSE_SUCCESS);
}

optparse_err_t optparse_remove_option (optparse_t *p, const char *name)
{
    optparse_err_t rc = OPTPARSE_SUCCESS;

    int n = list_delete_all (p->option_list,
                (ListFindF) option_info_cmp_name,
                (void *) name);

    if (n != 1)
        rc = OPTPARSE_FAILURE;

    return (rc);
}

optparse_err_t optparse_add_option_table (optparse_t *p,
        struct optparse_option const opts[])
{
    optparse_err_t rc = OPTPARSE_SUCCESS;
    const struct optparse_option *o = opts;

    while (o->usage) {
        if ((rc = optparse_add_option (p, o++)) != OPTPARSE_SUCCESS) {
            option_table_remove (p, opts, o);
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

    o.has_arg = 0;
    o.arginfo = NULL;
    o.cb    = NULL;
    o.name  = NULL;
    o.key   = 0;
    o.usage = doc;
    o.group = group;
    return optparse_add_option (p, &o);
}

optparse_err_t optparse_set (optparse_t *p, optparse_item_t item, ...)
{
    optparse_err_t e = OPTPARSE_SUCCESS;
    va_list vargs;
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


optparse_err_t optparse_get (optparse_t *p, optparse_item_t item, ...)
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
    case OPTPARSE_OPTION_WIDTH:
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
    char *optstring = malloc (2);
    if (optstring == NULL)
        return (NULL);
    optstring[0] = '+';
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
     */
    len = strlen (optstring);
    n = len + o->has_arg + 1;

    optstring = realloc (optstring, (n + 1)); /* extra character for NUL */

    if (o->has_arg == 1 || o->has_arg == 3)
        colons = ":";
    else if (o->has_arg == 2)
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
    ListIterator i;

    n = list_count (p->option_list);
    opts = malloc ((n + 1) * sizeof (struct option));
    if (opts == NULL)
        return (NULL);

    j = 0;
    i = list_iterator_create (p->option_list);
    while ((o = list_next (i))) {
        if (o->isdoc)
            continue;
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
    list_iterator_destroy (i);

    /*
     *  Initialize final element of option array to zeros:
     */
    memset (&opts[j], 0, sizeof (struct option));

    return (opts);
}

static int by_val (struct option_info *c, int *val)
{
    return (c->p_opt->key == *val);
}

static int opt_append_sep (struct option_info *opt, const char *s)
{
    error_t e;
    char *arg = NULL;
    char *argz = NULL;
    size_t len = 0;
    if ((e = argz_create_sep (s, ',', &argz, &len))) {
        errno = e;
        return (-1);
    }
    while ((arg = argz_next (argz, len, arg)))
        opt->optarg = list_append (opt->optargs, strdup (arg));

    free (argz);
    return (0);
}

static void opt_append_optarg (optparse_t *p, struct option_info *opt, const char *optarg)
{
    char *s;
    if (!opt->optargs)
        opt->optargs = list_create ((ListDelF) free);
    if (opt->autosplit) {
        if (opt_append_sep (opt, optarg) < 0)
            optparse_fatalmsg (p, 1, "%s: append '%s': %s\n", p->program_name,
                               optarg, strerror (errno));
        return;
    }
    if ((s = strdup (optarg)) == NULL)
        optparse_fatalmsg (p, 1, "%s: out of memory\n", p->program_name);
    list_append (opt->optargs, s);
    opt->optarg = s;
}

/*
 *  Call reentrant internal version of getopt_long() directly copied from
 *   glibc. See getopt.c and getopt_int.h in this directory.
 */
static int getopt_long_r (int argc, char *const *argv, const char *options,
                const struct option *long_options, int *opt_index,
                struct _getopt_data *d)
{
  return _getopt_internal_r (argc, argv, options, long_options, opt_index,
                             0, d, 0);
}

int optparse_parse_args (optparse_t *p, int argc, char *argv[])
{
    int c;
    int li = -1;
    struct _getopt_data d;
    const char *fullname = NULL;
    char *optstring = NULL;
    struct option *optz = option_table_create (p, &optstring);

    fullname = optparse_fullname (p);

    /* Initialize getopt internal state data:
     */
    memset (&d, 0, sizeof (d));

    while ((c = getopt_long_r (argc, argv, optstring, optz, &li, &d)) >= 0) {
        struct option_info *opt;
        struct optparse_option *o;
        if (c == '?') {
            (*p->log_fn) ("%s: unrecognized option '%s'\n",
                          fullname, argv[d.optind-1]);
            (*p->log_fn) ("Try `%s --help' for more information.\n", fullname);
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
            opt = list_find_first (p->option_list, (ListFindF) by_val, &c);

        /* Reset li for next iteration */
        li = -1;
        if (opt == NULL) {
            (*p->log_fn) ("ugh, didn't find option associated with char %c\n", c);
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
    p->optind = d.optind;
    return (p->optind);
}

int optparse_run_subcommand (optparse_t *p, int argc, char *argv[])
{
    int ac;
    char **av;
    optparse_subcmd_f cb;
    optparse_t *sp;

    if (p->optind == -1) {
        if (optparse_parse_args (p, argc, argv) < 0)
            return optparse_fatalerr (p, 1);
    }

    ac = argc - p->optind;
    av = argv + p->optind;

    if (ac <= 0)
        return optparse_fatal_usage (p, 1, "missing subcommand\n");

    if (!(sp = zhash_lookup (p->subcommands, av[0]))) {
        return optparse_fatal_usage (p, 1, "Unknown subcommand: %s\n", av[0]);
    }

    if (!sp->no_options && (optparse_parse_args (sp, ac, av) < 0))
        return optparse_fatalerr (sp, 1);

    if (!(cb = zhash_lookup (sp->dhash, "optparse::cb"))) {
        return optparse_fatalmsg (p, 1,
            "subcommand %s: failed to lookup callback!\n");
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


int optparse_optind (optparse_t *p)
{
    return (p->optind);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
