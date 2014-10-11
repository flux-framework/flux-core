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
#include <getopt.h>
#include <stdarg.h>

#include "src/common/liblsd/list.h"
#include "optparse.h"

/******************************************************************************
 *  Datatypes:
 *****************************************************************************/

/*
 *  Option parser data structure: (optparse_t implementation)
 */
struct opt_parser {
    char *         program_name;
    char *         usage;

    opt_log_f      log_fn;

    opt_fatalerr_f fatalerr_fn;
    void *         fatalerr_handle;

    int            left_margin;     /* Size of --help output left margin    */
    int            option_width;    /* Width of --help output for optiion   */
    int            current_group;   /* Current option group number          */
    List           option_list;     /* List of options for this program    */
};


/*
 *  Local option information structure:
 */
struct option_info {
    struct optparse_option * p_opt;   /* Copy of program option structure  */
    char *                  optarg;   /* If non-NULL, the option argument   */

    unsigned int            found:1;  /* 1 if this option has been used     */
    unsigned int            isdoc:1;  /* 1 if this is a 'doc-only' option   */
};

/******************************************************************************
 *  static Functions:
 *****************************************************************************/

static void optparse_option_destroy (struct optparse_option *o)
{
    if (o == NULL)
        return;
    o->arg = NULL;
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
        o->cb  =     src->cb;
        o->arg =     src->arg;
    }
    return (o);
}

static struct option_info *option_info_create (const struct optparse_option *o)
{
    struct option_info *c = malloc (sizeof (*c));
    if (c != NULL) {
        memset (c, 0, sizeof (*c));
        c->found = 0;
        c->optarg = NULL;
        c->p_opt = optparse_option_dup (o);
        if (!o->name)
            c->isdoc = 1;
    }
    return (c);
}

static void option_info_destroy (struct option_info *c)
{
    optparse_option_destroy (c->p_opt);
    free (c->optarg);
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
            return (1);
        else if (y->isdoc)
            return (-1);
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
static struct option_info *find_option_info (optparse_t p, const char *name)
{
    return (list_find_first (p->option_list,
                             (ListFindF) option_info_cmp_name,
                             (void *) name));
}

/*   Remove the options in table [opts], up to but not including [end]
 *    If [end] is NULL, remove all options.
 */
static void option_table_remove (optparse_t p,
        struct optparse_option const opts[],
        const struct optparse_option *end)
{
    const struct optparse_option *o = opts;

    while (o->usage || (end && o != end))
        optparse_remove_option (p, (o++)->name);

    return;
}

static int optparse_set_usage (optparse_t p, const char *usage)
{
    if (p->usage)
        free (p->usage);
    p->usage = strdup (usage);
    return (0);
}

static optparse_err_t optparse_set_log_fn (optparse_t p, opt_log_f fn)
{
    p->log_fn = fn;
    return (0);
}

static optparse_err_t optparse_set_fatalerr_fn (optparse_t p, opt_fatalerr_f fn)
{
    p->fatalerr_fn = fn;
    return (0);
}

static optparse_err_t optparse_set_fatalerr_handle (optparse_t p, void *handle)
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
static void log_stderr_exit (void *h, int exit_code, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    (void)vfprintf (stderr, fmt, ap);
    va_end (ap);
    exit (exit_code);
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
        strncpy (buf, seg, width+1);
        buf [width - 1]  = '-';
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
        if (p && (*p != '\0'))
            cols = (int) lval;
    }
    return (cols);
}

static void
optparse_doc_print (optparse_t p, struct optparse_option *o, int columns)
{
    char seg [128];
    char buf [4096];
    char *s;
    char *q;

    strncpy (buf, o->usage, sizeof (buf));
    q = buf;

    while ((s = get_next_segment (&q, columns, seg, sizeof (seg))))
        (*p->log_fn) ("%s\n", s);

    return;
}

static void
optparse_option_print (optparse_t p, struct optparse_option *o, int columns)
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
    strncpy (buf, o->usage, sizeof (buf));

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
        (*p->log_fn) ("\n%s\n%*s%s\n", info, width, "", s);

    /*  Get remaining usage lines (line-wrapped)
     */
    while ((s = get_next_segment (&q, descsiz, seg, sizeof (seg))))
        (*p->log_fn) ("%*s%s\n", width, "", s);

    return;
}

static int optparse_print_options (optparse_t p)
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

static int print_usage (optparse_t p)
{
    if (p->usage)
        (*p->log_fn) ("Usage: %s %s\n", p->program_name, p->usage);
    else
        (*p->log_fn) ("Usage: %s [OPTIONS]...\n", p->program_name);
    return optparse_print_options (p);
}

static int display_help (struct optparse_option *o, const char *optarg)
{
    print_usage (o->arg);
    exit (0);
}



/******************************************************************************
 *  API Functions:
 *****************************************************************************/

/*
 *  Destroy option parser [p]
 */
void optparse_destroy (optparse_t p)
{
    if (p == NULL)
        return;
    list_destroy (p->option_list);
    free (p);
}

/*
 *   Create option parser for program named [prog]
 */
optparse_t optparse_create (const char *prog)
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

    if (!(p->program_name = strdup (prog))) {
        free (p);
        return NULL;
    }
    p->usage = NULL;
    p->option_list = list_create ((ListDelF) option_info_destroy);
    if (!p->option_list) {
        free (p);
        return NULL;
    }

    p->log_fn = &log_stderr;
    p->fatalerr_fn = &log_stderr_exit;
    p->fatalerr_handle = NULL;
    p->left_margin = 2;
    p->option_width = 25;

    /*
     *  Register -h, --help
     */
    help.arg = (void *) p;
    if (optparse_add_option (p, &help) != OPTPARSE_SUCCESS) {
        fprintf (stderr, "failed to register --help option: %s\n", strerror (errno));
        optparse_destroy (p);
        return (NULL);
    }

    return (p);
}

/*
 *  Search for option with [name] in parser [p] returns -1 if option
 *   was never registered with this parser, 0 if option was not used,
 *   and 1 if option was specified in args. Returns pointer to optargp
 *   if non-NULL.
 */
int optparse_getopt (optparse_t p, const char *name, const char **optargp)
{
    struct option_info *c;
    if (!(c = find_option_info (p, name)))
        return (-1);

    if (c->found) {
        if (optargp)
            *optargp = c->optarg;
        return (1);
    }
    return (0);
}

bool optparse_hasopt (optparse_t p, const char *name)
{
    int n;
    if ((n = optparse_getopt (p, name, NULL)) < 0) {
        (*p->fatalerr_fn) (p->fatalerr_handle, 1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name, name);
        return false;
    }
    return (n > 0);
}

int optparse_get_int (optparse_t p, const char *name, int default_value)
{
    int n;
    long l;
    const char *s;
    char *endptr;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        (*p->fatalerr_fn) (p->fatalerr_handle, 1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name, name);
        return -1;
    }
    if (n == 0)
        return default_value;
    if (s == NULL || strlen (s) == 0)
        goto badarg;
    l = strtol (s, &endptr, 10);
    if (*endptr != '\0' ||  l < 0 || l > INT_MAX)
        goto badarg;
    return l;
badarg:
    (*p->fatalerr_fn) (p->fatalerr_handle, 1,
                       "%s: Option '%s' requires an integer argument\n",
                       p->program_name, name);
    return -1;
}

const char *optparse_get_str (optparse_t p, const char *name,
                              const char *default_value)
{
    int n;
    const char *s;

    if ((n = optparse_getopt (p, name, &s)) < 0) {
        (*p->fatalerr_fn) (p->fatalerr_handle, 1,
                           "%s: optparse error: no such argument '%s'\n",
                           p->program_name, name);
        return NULL;
    }
    if (n == 0)
        return default_value;
    return s;
}

optparse_err_t optparse_add_option (optparse_t p,
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

optparse_err_t optparse_remove_option (optparse_t p, const char *name)
{
    optparse_err_t rc = OPTPARSE_SUCCESS;

    int n = list_delete_all (p->option_list,
                (ListFindF) option_info_cmp_name,
                (void *) name);

    if (n != 1)
        rc = OPTPARSE_FAILURE;

    return (rc);
}

optparse_err_t optparse_add_option_table (optparse_t p,
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

optparse_err_t optparse_add_doc (optparse_t p, const char *doc, int group)
{
    struct optparse_option o;

    if (p == NULL || !p->option_list)
        return OPTPARSE_BAD_ARG;

    o.name  = NULL;
    o.key   = 0;
    o.usage = doc;
    o.group = group;
    return optparse_add_option (p, &o);
}

optparse_err_t optparse_set (optparse_t p, optparse_item_t item, ...)
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
    default:
        e = OPTPARSE_BAD_ARG;
    }
    va_end (vargs);
    return e;
}

optparse_err_t optparse_get (optparse_t p, optparse_item_t item, ...)
{
    /*
     *  Not implemented yet...
     */
    return OPTPARSE_NOT_IMPL;
}
static char * optstring_create ()
{
    char *optstring = malloc (1);
    *optstring = '\0';
    return (optstring);
}

static char * optstring_append (char *optstring, struct optparse_option *o)
{
    int n, len;
    char *colons = "";

    if (!isalnum (o->key))
        return (optstring);

    if (!optstring)
        optstring = optstring_create ();
    /*
     *  We need to add a single character to optstring for an
     *   an option with no argument (has_arg = 0), 2 characters
     *   for a required argument (has_arg = 1), and 3 chars for
     *   an optional argument "o::" (has_arg = 2)
     */
    len = strlen (optstring);
    n = len + o->has_arg + 1;

    optstring = realloc (optstring, (n + 1)); /* extra character for NUL */

    if (o->has_arg == 1)
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
static struct option * option_table_create (optparse_t p, char **sp)
{
    struct option_info *o;
    struct option *opts;
    int n;
    int j;
    ListIterator i;

    n = list_count (p->option_list);
    opts = malloc ((n + 1) * sizeof (struct option));

    j = 0;
    i = list_iterator_create (p->option_list);
    while ((o = list_next (i))) {
        if (o->isdoc)
            continue;
        /* Initialize option field from cached option structure */
        opt_init (&opts[j++], o->p_opt);
        if (sp)
            *sp = optstring_append (*sp, o->p_opt);
    }
    list_iterator_destroy (i);

    return (opts);
}

static int by_val (struct option_info *c, int *val)
{
    return (c->p_opt->key == *val);
}

int optparse_parse_args (optparse_t p, int argc, char *argv[])
{
    int c;
    int li;
    char *optstring = NULL;
    struct option *optz = option_table_create (p, &optstring);


    while ((c = getopt_long (argc, argv, optstring, optz, &li))) {
        struct option_info *opt;
        struct optparse_option *o;
        if (c == -1)
            break;
        if (c == '?') {
            fprintf (stderr, "Unknown option. Try --help\n");
            optind = -1;
            break;
        }

        opt = list_find_first (p->option_list, (ListFindF) by_val, &c);
        if (opt == NULL) {
            fprintf (stderr, "ugh, didn't find option associated with char %c\n", c);
            continue;
        }

        opt->found++;
        if (optarg) {
            if (opt->optarg)
                free (opt->optarg);
            opt->optarg = strdup (optarg);
        }

        o = opt->p_opt;
        if (o->cb && ((o->cb) (o, optarg, o->arg) < 0)) {
            fprintf (stderr, "Option \"%s\" failed\n", o->name);
            return (-1);
        }
    }

    free (optz);
    free (optstring);

    return (optind);
}

int optparse_print_usage (optparse_t p)
{
    return print_usage (p);
}

#ifdef TEST_MAIN
#include "src/common/libtap/tap.h"

static void *myfatal_h = NULL;

void myfatal (void *h, int exit_code, const char *fmt, ...)
{
    myfatal_h = h;
}

void test_convenience_accessors (void)
{
    struct optparse_option opts [] = {
{ .name = "foo", .key = 1, .has_arg = 0,                .usage = "" },
{ .name = "bar", .key = 2, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "baz", .key = 3, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "mnf", .key = 4, .has_arg = 1, .arginfo = "", .usage = "" },
{ .name = "oop", .key = 5, .has_arg = 1, .arginfo = "", .usage = "" },
        OPTPARSE_TABLE_END,
    };

    char *av[] = { "test", "--foo", "--baz=hello", "--mnf=7", NULL };
    int ac = sizeof (av) / sizeof (av[0]) - 1;
    int rc, optind;

    optparse_t p = optparse_create ("test");
    ok (p != NULL, "create object");

    rc = optparse_add_option_table (p, opts);
    ok (rc == OPTPARSE_SUCCESS, "register options");

    optind = optparse_parse_args (p, ac, av);
    ok (optind == ac, "parse options, verify optind");

    /* hasopt
     */
    dies_ok ({ optparse_hasopt (p, "no-exist"); },
            "hasopt exits on unknown arg");
    lives_ok ({ optparse_hasopt (p, "foo"); },
            "hasopt lives on known arg");
    ok (optparse_hasopt (p, "foo"), "hasopt finds present option");
    ok (!optparse_hasopt (p, "bar"), "hasopt doesn't find missing option");
    ok (optparse_hasopt (p, "baz"), "hasopt finds option with argument");

    /* get_int
     */
    dies_ok ({optparse_get_int (p, "no-exist", 0); },
            "get_int exits on unknown arg");
    dies_ok ({optparse_get_int (p, "foo", 0); },
            "get_int exits on option with no argument");
    dies_ok ({optparse_get_int (p, "baz", 0); },
            "get_int exits on option with wrong type argument");
    lives_ok ({optparse_get_int (p, "bar", 0); },
            "get_int lives on known arg");
    ok (optparse_get_int (p, "bar", 42) == 42,
            "get_int returns default argument when arg not present");
    ok (optparse_get_int (p, "mnf", 42) == 7,
            "get_int returns arg when present");

    /* get_str
     */
    dies_ok ({optparse_get_str (p, "no-exist", NULL); },
            "get_str exits on unknown arg");
    ok (optparse_get_str (p, "foo", "xyz") == NULL,
            "get_str returns NULL on option with no argument configured");
    lives_ok ({optparse_get_str (p, "bar", NULL); },
            "get_str lives on known arg");
    ok (optparse_get_str (p, "bar", NULL) == NULL,
            "get_str returns default argument when arg not present");
    like (optparse_get_str (p, "baz", NULL), "^hello$",
            "get_str returns arg when present");

    /* fatalerr
     */
    dies_ok ({ optparse_hasopt (p, "no-exist"); },
            "hasopt exits on unknown arg");

    rc = optparse_set (p, OPTPARSE_FATALERR_FN, myfatal);
    ok (rc == OPTPARSE_SUCCESS, "optparse_set FATALERR_FN");
    rc = optparse_set (p, OPTPARSE_FATALERR_HANDLE, stderr);
    ok (rc == OPTPARSE_SUCCESS, "optparse_set FATALERR_HANDLE");
    lives_ok ({optparse_get_int (p, "no-exist", 0); },
            "get_int now survives unknown arg");
    ok (myfatal_h == stderr, "handle successfully passed to fatalerr");
    optparse_destroy (p);
}

int main (int argc, char *argv[])
{

    plan (24);

    test_convenience_accessors (); /* 24 tests */

    done_testing ();
    return (0);
}

#endif /* TEST_MAIN */

/*
 * vi: ts=4 sw=4 expandtab
 */
