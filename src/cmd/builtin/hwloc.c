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
#include "builtin.h"

#include <sys/types.h> /* WIFEXTED */
#include <sys/wait.h>
#include <sys/param.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <argz.h>
#include <inttypes.h>
#include <jansson.h>

#include <hwloc.h>

#include "src/common/libidset/idset.h"
#include "src/common/librlist/rhwloc.h"

/*  idset helpers:
 */

/*  Return an idset with all ranks set for the current Flux instance:
 */
static struct idset *idset_all (uint32_t size)
{
    struct idset *idset = NULL;
    if (!(idset = idset_create (size, 0))
        || (idset_range_set (idset, 0, size-1) < 0)) {
        idset_destroy (idset);
        return NULL;
    }
    return (idset);
}

/*  Return an idset from the character string "ranks", returning all
 *   current ranks for "all"
 */
static struct idset *ranks_to_idset (flux_t *h, const char *ranks)
{
    uint32_t size;
    struct idset *idset;

    if (flux_get_size (h, &size) < 0)
        return NULL;

    if (strcmp (ranks, "all") == 0)
        idset = idset_all (size);
    else {
        idset = idset_decode (ranks);
        if (idset_count (idset) > 0 && idset_last (idset) > size - 1) {
            log_msg ("Invalid rank argument: '%s'", ranks);
            idset_destroy (idset);
            return (NULL);
        }
    }
    return (idset);
}

static int lookup_all_topo_xml (flux_t *h, char **xmlv, struct idset *ids)
{
    int rc = -1;
    unsigned int i;
    int n;
    json_t *xml_array = NULL;
    flux_future_t *f = NULL;

    if (!(f = flux_rpc (h, "resource.get-xml", NULL, 0, 0)))
        goto err;
    if (flux_rpc_get_unpack (f, "{s:o}", "xml", &xml_array) < 0)
        goto err;

    i = idset_first (ids);
    n = 0;
    while (i != IDSET_INVALID_ID) {
        const char *s;
        json_t *o = json_array_get (xml_array, i);
        if (!o) {
            log_msg ("resource.get-xml: rank %u not found in response", i);
            goto err;
        }
        if (!(s = json_string_value (o))) {
            log_msg ("resource.get-xml: rank %u: not a string value", i);
            goto err;
        }
        if (!(xmlv[n++] = strdup (s))) {
            log_msg ("resource.get-xml: Out of memory");
            goto err;
        }
        i = idset_next (ids, i);
    }
    rc = 0;
err:
    flux_future_destroy (f);
    return rc;
}


static void string_array_destroy (char **arg, uint32_t n)
{
    int i;
    for (i = 0; i < n; i++)
        free (arg[i]);
    free (arg);
}

void flux_hwloc_global_xml (optparse_t *p, char ***xmlv, uint32_t *size)
{
    flux_t *h = NULL;
    const char *arg;
    struct idset *idset = NULL;

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    if (optparse_getopt (p, "rank", &arg) <= 0)
        arg = "all";

    if (!(idset = ranks_to_idset (h, arg)))
        log_msg_exit ("failed to get target ranks");

    if ((*size = idset_count (idset)) == 0)
        log_msg_exit ("Invalid rank set when fetching global XML");

    if (!(*xmlv = calloc (*size, sizeof (char *))))
        log_msg_exit ("failed to alloc array for %"PRIu32" ranks", *size);

    if (lookup_all_topo_xml (h, *xmlv, idset) < 0)
        log_err_exit ("gather: failed to get all topo xml");

    idset_destroy (idset);
    flux_close (h);
}

/*  HWLOC topology helpers:
 */

/*
 *  Return hwloc XML as a malloc()'d string. Returns the topology of this
 *   system if "--local" is set in the optparse object `p`, otherwise
 *   returns the global XML. Caller must free the result.
 */
static void flux_hwloc_xml (optparse_t *p, char ***xmlv, uint32_t *size)
{
    if (optparse_hasopt (p, "local")) {
        *size = 1;
        *xmlv = calloc(*size, sizeof(char *));
        *xmlv[0] = rhwloc_local_topology_xml ();
    }
    else
        flux_hwloc_global_xml (p, xmlv, size);
}

#if HWLOC_API_VERSION >= 0x20000
static void print_topologies (char **xmlv, int size)
{
    /*  hwloc 2.x dropped support for hwloc_custom,
     *  so just dump the array of XML
     */
    for (int i = 0; i < size; i++)
        puts (xmlv[i]);
}
#else
static void print_topologies (char **xmlv, int size)
{
    hwloc_topology_t global = NULL;
    char *xml = NULL;
    int xmlsize = 0;

    /*  Create custom topology for concatenating all global hwloc XML
     */
    if (hwloc_topology_init (&global) < 0)
        log_err_exit ("hwloc_topology_init (global)");
    hwloc_topology_set_custom (global);

    /*  Iterate over all returned XML and insert results into global topo:
     */
    for (int i = 0; i < size; i++) {
        hwloc_topology_t rank;

        if (hwloc_topology_init (&rank) < 0)
            log_err_exit ("hwloc_topology_init");
        if (hwloc_topology_set_xmlbuffer (rank, xmlv[i], strlen (xmlv[i])) < 0
            || hwloc_topology_load (rank)
            || hwloc_custom_insert_topology (global,
                                             hwloc_get_root_obj (global),
                                             rank,
                                             NULL) < 0)
            log_err_exit ("failed to insert xml %d into global topology", i);

        hwloc_topology_destroy (rank);
    }

    hwloc_topology_load (global);
    if (hwloc_topology_export_xmlbuffer (global, &xml, &xmlsize) < 0)
        log_err_exit ("hwloc_topology_export_xmlbuffer");

    puts (xml);
    hwloc_free_xmlbuffer (global, xml);
    hwloc_topology_destroy (global);
}
#endif

/*  flux-hwloc topology:
 */
static int cmd_topology (optparse_t *p, int ac, char *av[])
{
    char **xmlv = NULL;
    uint32_t size = 0;

    /*  Fetch XML array from resource module:
     */
    flux_hwloc_xml (p, &xmlv, &size);

    /*  Dump XML to stdout
     */
    print_topologies (xmlv, size);

    string_array_destroy (xmlv, size);
    return (0);
}

/*  flux-hwloc info:
 */

static int gpu_count (hwloc_topology_t topo)
{
    int count = 0;
    struct idset *ids = idset_decode (rhwloc_gpu_idset_string (topo));
    if (ids) {
        count = idset_count (ids);
        idset_destroy (ids);
    }
    return count;
}

static int cmd_info (optparse_t *p, int ac, char *av[])
{
    char **xmlv = NULL;
    char *xml = NULL;
    uint32_t size = 0, i = 0;
    int ncores = 0, npu = 0, nnodes = 0, ngpus = 0;
    hwloc_topology_t topo;

    flux_hwloc_xml (p, &xmlv, &size);

    for (i = 0; i < size; i++) {
        xml = xmlv[i];
        if (!xml || !(topo = rhwloc_xml_topology_load (xml)))
            log_msg_exit ("info: Failed to initialize topology from XML");

        ncores += hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_CORE);
        npu    += hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_PU);
        nnodes += hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_MACHINE);
        ngpus += gpu_count (topo);
        hwloc_topology_destroy (topo);
    }

    printf ("%d Machine%s, %d Cores, %d PUs",
            nnodes, nnodes > 1 ? "s" : "", ncores, npu);
    if (ngpus > 0) {
        printf (", %d GPU%s\n", ngpus, ngpus > 1 ? "s" : "");
    } else {
        printf ("\n");
    }

    string_array_destroy (xmlv, size);
    return (0);
}

/*  flux-hwloc:
 */

int cmd_hwloc (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-hwloc");
    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option topology_opts[] = {
    { .name = "local", .key = 'l', .has_arg = 0,
      .usage = "Dump topology XML for the local host only",
    },
    { .name = "rank", .key = 'r', .has_arg = 1,
      .usage = "Target specified nodeset, or \"all\" (default)",
    },
    OPTPARSE_TABLE_END,
};

static struct optparse_subcommand hwloc_subcmds[] = {
    { "topology",
      NULL,
      "Dump system topology XML to stdout",
      cmd_topology,
      0,
      topology_opts,
    },
    { "info",
      NULL,
      "Short-form dump of instance resources",
      cmd_info,
      0,
      topology_opts,
    },
    OPTPARSE_SUBCMD_END,
};

int subcommand_hwloc_register (optparse_t *p)
{
    optparse_t *c;
    optparse_err_t e;

    e = optparse_reg_subcommand (p, "hwloc", cmd_hwloc, NULL,
                                 "Control/query resource-hwloc service",
                                 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    c = optparse_get_subcommand (p, "hwloc");
    if ((e = optparse_reg_subcommands (c, hwloc_subcmds)) != OPTPARSE_SUCCESS)
        return (-1);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
