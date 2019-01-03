/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <czmq.h>
#include <jansson.h>
#include "builtin.h"

static struct optparse_option setattr_opts[] = {
    { .name = "expunge", .key = 'e', .has_arg = 0,
      .usage = "Unset the specified attribute",
    },
    OPTPARSE_TABLE_END
};

static int cmd_setattr (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    flux_t *h = builtin_get_flux_handle (p);
    const char *name;
    const char *val;

    log_init ("flux-setattr");

    if (optparse_hasopt (p, "expunge")) {
        if (n != ac - 1) {
            optparse_print_usage (p);
            exit (1);
        }
        name = av[n];
        if (flux_attr_set (h, name, NULL) < 0)
            log_err_exit ("flux_attr_set");
    }
    else {
        if (n != ac - 2) {
            optparse_print_usage (p);
            exit (1);
        }
        name = av[n];
        val = av[n + 1];
        if (flux_attr_set (h, name, val) < 0)
            log_err_exit ("flux_attr_set");
    }

    flux_close (h);
    return (0);
}

static struct optparse_option lsattr_opts[] = {
    { .name = "values", .key = 'v', .has_arg = 0,
      .usage = "List values with attributes",
    },
    OPTPARSE_TABLE_END
};

void attrfree (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

void *attrdup (const void *item)
{
    return strdup (item);
}

int attrcmp(const void *item1, const void *item2)
{
    return strcmp (item1, item2);
}

/* Get list of attributes from the broker, then insert
 * into a sorted list and return it.
 */
zlistx_t *get_sorted_attrlist (flux_t *h)
{
    flux_future_t *f;
    zlistx_t *list;
    json_t *names; // array of attr names
    size_t index;
    json_t *value;

    if (!(list = zlistx_new ()))
        log_err_exit ("zlistx_new");
    zlistx_set_comparator (list, attrcmp);
    zlistx_set_duplicator (list, attrdup);
    zlistx_set_destructor (list, attrfree);
    if (!(f = flux_rpc (h, "attr.list", NULL, FLUX_NODEID_ANY, 0))
                || flux_rpc_get_unpack  (f, "{s:o}", "names", &names) < 0)
        log_err_exit ("attr.list");
    json_array_foreach (names, index, value) {
        const char *name = json_string_value (value);
        if (!name)
            log_msg_exit ("non-string attr name returned");
        if (!zlistx_insert (list, (char *)name, false))
            log_msg_exit ("zlistx_insert failed");
    }
    flux_future_destroy (f);
    return list;
}

static int cmd_lsattr (optparse_t *p, int ac, char *av[])
{
    flux_t *h = builtin_get_flux_handle (p);
    int n = optparse_option_index (p);
    const char *name, *val;
    zlistx_t *list;

    log_init ("flux-lsatrr");

    if (n != ac)
        optparse_fatal_usage (p, 1, NULL);

    list = get_sorted_attrlist (h);
    name = zlistx_first (list);
    while (name) {
        if (optparse_hasopt (p, "values")) {
            val = flux_attr_get (h, name);
            printf ("%-40s%s\n", name, val ? val : "-");
        } else {
            printf ("%s\n", name);
        }
        name = zlistx_next (list);
    }
    zlistx_destroy (&list);
    flux_close (h);
    return (0);
}

static int cmd_getattr (optparse_t *p, int ac, char *av[])
{
    flux_t *h = builtin_get_flux_handle (p);
    int n = optparse_option_index (p);
    const char *name, *val;

    log_init ("flux-getattr");

    if (n != ac - 1)
        optparse_fatal_usage (p, 1, NULL);
    name = av[n];
    if (!(val = flux_attr_get (h, name)))
        log_err_exit ("%s", name);
    printf ("%s\n", val);
    flux_close (h);
    return (0);
}

int subcommand_attr_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p, "setattr", cmd_setattr,
        "name value",
        "Set broker attribute",
        0,
        setattr_opts);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommand (p, "getattr", cmd_getattr,
        "name",
        "Get broker attribute",
        0,
        NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommand (p, "lsattr", cmd_lsattr,
        "[-v]",
        "List broker attributes",
        0,
        lsattr_opts);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
