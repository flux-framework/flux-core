/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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
#include "src/common/libaggregate/aggregate.h"
#include "src/common/libutil/monotime.h"

#define XML_BASEDIR "resource.hwloc.xml"

extern char **environ;

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

/*  Topology kvs helpers:
 */

static void lookup_continuation (flux_future_t *f, void *arg)
{
    char **valp = arg;
    const char *xml;

    if (flux_kvs_lookup_get_unpack (f, "s", &xml) < 0)
        log_err_exit ("unable to unpack rank xml");

    *valp = strdup (xml);

    flux_future_destroy (f);
}

/*  Send lookup request for topology XML for all ranks in idset, returning
 *   copies of each XML in xmls array (The array must have space for
 *   idset_count (idset) members).
 *  There should be no other watchers registered on the main handle reactor
 *   here, so it is safe to drop into the handle reactor and return when
 *   all lookup handlers have completed.
 */
static int lookup_all_topo_xml (flux_t *h, char **xmls, struct idset *idset)
{
    flux_future_t *f = NULL;
    char key [1024];
    int rank = idset_first (idset);
    int i = 0;

    while (rank != IDSET_INVALID_ID) {
        snprintf (key, sizeof (key), "%s.%d", XML_BASEDIR, rank);
        if (!(f = flux_kvs_lookup (h, NULL, 0, key))
            || (flux_future_then (f, -1., lookup_continuation, &xmls[i]) < 0))
            log_err_exit ("kvs lookup");

        rank = idset_next (idset, rank);
        i++;
    }
    return (flux_reactor_run (flux_get_reactor (h), 0));
}

/*  Lookup topo XML for a single rank using degenerate case of
 *  lookup_all_topo_xml()
 */
static int lookup_one_topo_xml (flux_t *h, char **valp, uint32_t rank)
{
    int rc;
    struct idset *idset = idset_create (0, IDSET_FLAG_AUTOGROW);
    if (!idset || idset_set (idset, rank) < 0) {
       log_err ("idset_create/set rank=%d", rank);
        return (-1);
    }
    rc = lookup_all_topo_xml (h, valp, idset);
    idset_destroy (idset);
    return (rc);
}

/*  Given an array of topology XML strings with `n` entries, return
 *   a "global" topology object.
 */
static hwloc_topology_t global_hwloc_create (char **xml, int n)
{
    hwloc_topology_t global;
    int i;

    if (hwloc_topology_init (&global) < 0
        || hwloc_topology_set_custom (global) < 0)
        log_err_exit ("gather: unable to init topology");

    for (i = 0; i < n; i++) {
        hwloc_topology_t topo;
        if (hwloc_topology_init (&topo) < 0)
            log_err_exit ("hwloc_topology_init");
        if (hwloc_topology_set_xmlbuffer (topo, xml[i], strlen(xml[i])+1) < 0)
            log_err_exit ("hwloc_topology_set_xmlbuffer");
        if (hwloc_topology_load (topo) < 0)
            log_err_exit ("hwloc_topology_load");
        if (hwloc_custom_insert_topology (global,
                                          hwloc_get_root_obj (global),
                                          topo, NULL) < 0)
            log_err_exit ("hwloc_custom_insert_topo");
        hwloc_topology_destroy (topo);
    }
    hwloc_topology_load (global);

    return (global);
}

static void string_array_destroy (char **arg, int n)
{
    int i;
    for (i = 0; i < n; i++)
        free (arg[i]);
    free (arg);
}

char * flux_hwloc_global_xml (optparse_t *p)
{
    flux_t *h = NULL;
    uint32_t size;
    char *xml;
    char *buf;
    const char *arg;
    int buflen;
    struct idset *idset = NULL;
    hwloc_topology_t global = NULL;
    char **xmlv = NULL;

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    if (optparse_getopt (p, "rank", &arg) <= 0)
        arg = "all";
    if (!(idset = ranks_to_idset (h, arg)))
        log_msg_exit ("failed to get target ranks");

    if ((size = idset_count (idset)) == 0)
        log_msg_exit ("Invalid rank set when fetching global XML");

    if (!(xmlv = calloc (size, sizeof (char *))))
        log_msg_exit ("failed to alloc array for %d ranks", size);

    if (lookup_all_topo_xml (h, xmlv, idset) < 0)
        log_err_exit ("gather: failed to get all topo xml");

    if (!(global = global_hwloc_create (xmlv, size)))
        log_err_exit ("gather: failed create global xml");

    if (hwloc_topology_export_xmlbuffer (global, &buf, &buflen) < 0)
        log_err_exit ("hwloc export XML");

    /* XML buffer must be destroyed by hwloc_free_xmlbuffer, so copy it here */
    xml = strdup (buf);

    hwloc_free_xmlbuffer (global, buf);
    string_array_destroy (xmlv, size);
    hwloc_topology_destroy (global);
    idset_destroy (idset);
    flux_close (h);
    return (xml);
}

/*  HWLOC topology helpers:
 */

/*  Common hwloc_topology_init() and flags for Flux hwloc usage:
 */
static void topo_init_common (hwloc_topology_t *tp)
{
    if (hwloc_topology_init (tp) < 0)
        log_err_exit ("hwloc_topology_init");
    if (hwloc_topology_set_flags (*tp, HWLOC_TOPOLOGY_FLAG_IO_DEVICES) < 0)
        log_err_exit ("hwloc_topology_set_flags");
    if (hwloc_topology_ignore_type (*tp, HWLOC_OBJ_CACHE) < 0)
        log_err_exit ("hwloc_topology_ignore_type OBJ_CACHE failed");
    if (hwloc_topology_ignore_type (*tp, HWLOC_OBJ_GROUP) < 0)
        log_err_exit ("hwloc_topology_ignore_type OBJ_GROUP failed");
}

/*  Load the local topology in a manner most useful to Flux components,
 *   i.e. grab IO devices, ignore cache and group objects, and mask off
 *   objects not in our cpuset.
 */
static hwloc_topology_t local_topo_load (void)
{
    hwloc_topology_t topo;
    hwloc_bitmap_t rset = NULL;
    uint32_t hwloc_version = hwloc_get_api_version ();

    if ((hwloc_version >> 16) != (HWLOC_API_VERSION >> 16))
        log_err_exit ("compiled for hwloc 0x%x but running against 0x%x\n",
                      HWLOC_API_VERSION, hwloc_version);

    topo_init_common (&topo);

    if (hwloc_topology_load (topo) < 0)
        log_err_exit ("hwloc_topology_load");
    if (!(rset = hwloc_bitmap_alloc ())
        || (hwloc_get_cpubind (topo, rset, HWLOC_CPUBIND_PROCESS) < 0))
        log_err_exit ("hwloc_get_cpubind");
    if (hwloc_topology_restrict (topo, rset, 0) < 0)
        log_err_exit ("hwloc_topology_restrict");
    hwloc_bitmap_free (rset);
    return (topo);
}

static char *flux_hwloc_local_xml (void)
{
    char *buf;
    int buflen;
    char *copy;
    hwloc_topology_t topo = local_topo_load ();
    if (topo == NULL)
        return (NULL);
    if (hwloc_topology_export_xmlbuffer (topo, &buf, &buflen) < 0)
        log_err_exit ("Failed to export hwloc to XML");
    copy = strdup (buf);
    hwloc_free_xmlbuffer (topo, buf);
    return (copy);
}

/*
 *  Return hwloc XML as a malloc()'d string. Returns the topolog of this
 *   system if "--local" is set in the optparse object `p`, otherwise
 *   returns the global XML. Caller must free the result.
 */
static char *flux_hwloc_xml (optparse_t *p)
{
    if (optparse_hasopt (p, "local"))
        return flux_hwloc_local_xml ();
    return flux_hwloc_global_xml (p);
}

static int argz_appendf (char **argzp, size_t *argz_len, const char *fmt, ...)
{
    char s [4096];
    int rc = -1;
    int n;
    va_list ap;

    va_start (ap, fmt);
    if ((n = vsnprintf (s, sizeof (s), fmt, ap) < 0) || (n >= sizeof (s)))
        goto out;
    va_end (ap);

    if ((errno = argz_add_sep (argzp, argz_len, s, ' ')) == 0)
        rc = 0;
out:
    return (rc);
}

static void lstopo_argz_init (char *cmd, char **argzp, size_t *argz_lenp,
                              char *extra_args[])
{
    char *extra;
    size_t extra_len;
    int e;
    char *argv[] = { cmd, "-i", "-", "--if", "xml",
                     "--of", "console", NULL };
    if (  (e = argz_create (argv, argzp, argz_lenp)) != 0
       || (e = argz_create (extra_args, &extra, &extra_len)) != 0)
        log_msg_exit ("argz_create: %s", strerror (e));

    /*  Append any extra args in av[] */
    if ((e = argz_append (argzp, argz_lenp, extra, extra_len)) != 0)
        log_msg_exit ("argz_append: %s", strerror (e));

    free (extra);
}

int argz_execp (char *argz, size_t argz_len)
{
    char *argv [argz_count (argz, argz_len) + 1];
    argz_extract (argz, argz_len, argv);
    return execvp (argv[0], argv);
}

FILE * argz_popen (char *argz, size_t argz_len, pid_t *pidptr)
{
    int pfds[2];
    pid_t pid;
    if (pipe (pfds) < 0)
        log_err_exit ("pipe");
    switch ((pid = fork ())) {
    case -1:
        log_err_exit ("fork");
    case  0:
        close (pfds[1]);
        dup2 (pfds[0], STDIN_FILENO);
        argz_execp (argz, argz_len);
        if (errno != ENOENT)
            log_err ("exec");
        exit (errno); /* So we can detect ENOENT.. Sorry */
    default:
        break;
    }
    close (pfds[0]);
    *pidptr = pid;
    return (fdopen (pfds[1], "w"));
}

/*
 *  Execute lstopo from user's PATH sending full topology XML over stdin.
 *   Pass any extra options along to lstopo(1).
 *
 *  If running lstopo fails with ENOENT, try lstopo-no-graphics.
 */
static int exec_lstopo (optparse_t *p, int ac, char *av[], const char *topo)
{
    int status;
    FILE *fp;
    char *argz;
    size_t argz_len;
    pid_t pid;
    const char *cmds[] = { "lstopo", "lstopo-no-graphics", NULL };
    const char **cp = cmds;

    /* Ignore SIGPIPE so we don't get killed when exec() fails */
    signal (SIGPIPE, SIG_IGN);

    /* Initialize argz with first command in cmds above: */
    lstopo_argz_init ((char *) *cp, &argz, &argz_len, av+1);

    while (true) {
        const char *next = *(cp+1);
        if (!(fp = argz_popen (argz, argz_len, &pid)))
            log_err_exit ("popen (lstopo)");
        fputs (topo, fp);
        fclose (fp);
        if (waitpid (pid, &status, 0) < 0)
            log_err_exit ("waitpid");

        /* Break out of loop if exec() was succcessful, failed with
         *  an error other than "File not found", or we ran out programs
         *  to try.
         */
        if (status == 0 || !next || WEXITSTATUS (status) != ENOENT)
            break;

        /* Replace previous cmd in argz with next command to try:
         */
        argz_replace (&argz, &argz_len, *(cp++), next, NULL);
    }

    return (status);
}

static int cmd_lstopo (optparse_t *p, int ac, char *av[])
{
    int status;
    char *xml = flux_hwloc_global_xml (p);
    if (xml == NULL)
        log_msg_exit ("Failed to fetch global hwloc XML");

    status = exec_lstopo (p, ac, av, xml);
    if (status) {
        if (WIFEXITED (status)) {
            if (WEXITSTATUS (status) == ENOENT)
                log_msg_exit ("Unable to find an lstopo variant to execute.");
            else
                log_msg_exit ("lstopo: Exited with %d", WEXITSTATUS (status));
        }
        if (WIFSIGNALED (status) && WTERMSIG (status) != SIGPIPE)
            log_msg_exit ("lstopo: %s%s", strsignal (WTERMSIG (status)),
                      WCOREDUMP (status) ? " (core dumped)" : "");
    }

    free (xml);

    return (0);
}

/*  flux-hwloc topology:
 */

static int cmd_topology (optparse_t *p, int ac, char *av[])
{
    char *xml = flux_hwloc_xml (p);
    puts (xml);
    free (xml);
    return (0);
}

/*  flux-hwloc info:
 */

/*  Initialize a hwloc toplogy from xml string `xml`, applying the common
 *   flags and options for Flux usage.
 */
static int init_topo_from_xml (hwloc_topology_t *tp, const char *xml)
{
    topo_init_common (tp);
    if ((hwloc_topology_set_xmlbuffer (*tp, xml, strlen (xml) + 1) < 0)
        || (hwloc_topology_load (*tp) < 0)) {
        hwloc_topology_destroy (*tp);
        return (-1);
    }
    return (0);
}

static int cmd_info (optparse_t *p, int ac, char *av[])
{
    char *xml = flux_hwloc_xml (p);
    hwloc_topology_t topo;

    if (!xml || init_topo_from_xml (&topo, xml) < 0)
        log_msg_exit ("info: Failed to initialize topology from XML");

    int ncores = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_CORE);
    int npu    = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_PU);
    int nnodes = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_MACHINE);

    printf ("%d Machine%s, %d Cores, %d PUs\n",
            nnodes, nnodes > 1 ? "s" : "", ncores, npu);

    hwloc_topology_destroy (topo);
    free (xml);
    return (0);
}

/*  flux-hwloc reload:
 */

/*  Add hwloc xml string `xml` to kvs for rank `rank` to a kvs txn
 */
static int kvs_txn_put_xml (flux_kvs_txn_t *txn, uint32_t rank,
                             const char *xml)
{
    char key [1024];
    snprintf (key, sizeof (key), "%s.%ju", XML_BASEDIR, (uintmax_t) rank);
    return (flux_kvs_txn_pack (txn, 0, key, "s", xml));
}

/*  Add hwloc xml from file at path <basedir>/<rank>.xml to kvs for
 *   rank. The topology is first loaded into a hwloc_topology_t object
 *   so that the common Flux hwloc flags may be applied,  and to check
 *   that the XML is valid before putting it in the kvs.
 */
static flux_future_t *kvs_txn_put_xml_file (flux_kvs_txn_t *txn, int rank,
                                            const char *basedir)
{
    char path [8192];
    int n, len;
    char *xml;
    hwloc_topology_t topo = NULL;

    if ((n = snprintf (path, sizeof (path), "%s/%d.xml", basedir, rank) < 0)
        || (n >= sizeof (path)))
        log_err_exit ("failed to create xml path");

    topo_init_common (&topo);

    if (hwloc_topology_set_xml (topo, path) < 0)
        log_err_exit ("Unable to set XML to path=%s", path);

    if (hwloc_topology_load (topo) < 0)
        log_err_exit ("hwloc_topology_load (%s)", path);

    if (hwloc_topology_export_xmlbuffer (topo, &xml, &len) < 0)
        log_err_exit ("hwloc_topology_export_xmlbuffer");

    if (kvs_txn_put_xml (txn, rank, xml) < 0)
        log_err_exit ("kvs_txn_put_xml");

    hwloc_free_xmlbuffer (topo, xml);
    hwloc_topology_destroy (topo);
    return (0);
}

/*  Load XML for all ranks in `idset` from files in `basedir`, one per
 *   rank: <basedir>/<rank>.xml. All KVS puts are performed under a
 *   single transaction.
 */
flux_future_t * kvs_load_xml_idset (flux_t *h, const char *basedir,
                                    struct idset *idset)
{
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn = NULL;
    unsigned int rank = idset_first (idset);

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");

    while (rank != IDSET_INVALID_ID) {
        kvs_txn_put_xml_file (txn, rank, basedir);
        rank = idset_next (idset, rank);
    }
    if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
        log_err_exit ("flux_kvs_commit request");
    flux_kvs_txn_destroy (txn);
    return (f);
}

static double seconds_since (struct timespec t)
{
    return (monotime_since (t)/1000.);
}

/*  Execute flux-hwloc aggregate-load across all ranks, optionally
 *   reloading local hwloc XML on `reload_ranks`.
 */
static int run_hwloc_aggregate (flux_t *h, const char *ranks, bool verbose,
                                struct timespec t0)
{
    const char *base = "resource.hwloc";
    char *argz = NULL;
    size_t argz_len = 0;
    uint32_t rank, size;
    double timeout = 5.;
    char *argv[] = {
        "flux", "exec", "-n", "-r", "all", NULL
    };

    if (flux_get_rank (h, &rank) < 0)
        log_err_exit ("flux_get_rank");
    if (flux_get_size (h, &size) < 0)
        log_err_exit ("flux_get_rank");

    /* XXX: scale timeout by size just in case.. */
    if (size > 512)
        timeout = timeout + size/512.;

    if ((errno = argz_create (argv, &argz, &argz_len)))
        log_err_exit ("exec aggregate-load: argz_create");

    /*  Append -v to flux-exec if verbose */
    if (verbose && (argz_appendf (&argz, &argz_len, "-v")))
        log_err_exit ("exec aggregate-load: argz_appendf");

    /*  Build flux hwloc aggregate-load command */
    if ((argz_appendf (&argz, &argz_len,
                "flux hwloc aggregate-load "
                "--timeout=%.3f --unpack=%s.by_rank --key=%s.reload:%u-%u",
                timeout, base, base, rank, getpid()) < 0)
        || (ranks && (argz_appendf (&argz, &argz_len, "--rank=%s", ranks) < 0))
        || (verbose && argz_appendf (&argz, &argz_len, "--verbose")))
        log_err_exit ("argz_appendf flux-hwloc aggregate-load command");

    if (verbose) {
        char copy [argz_len];
        memcpy (copy, argz, argz_len);
        argz_stringify (copy, argz_len, ' ');
        log_msg ("%.3fs: Running %s", seconds_since (t0), copy);
    }
    argz_execp (argz, argz_len);

    log_err_exit ("exec: flux-exec flux hwloc aggregate-load");
}

static int internal_hwloc_reload (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    const char *default_nodeset = "all";
    const char *nodeset = optparse_get_str (p, "rank", default_nodeset);
    uint32_t size;
    bool verbose;
    struct timespec t0;
    struct idset *idset = NULL;
    char *dirpath = NULL;
    char *reload_ranks;
    flux_t *h;

    if ((verbose = optparse_hasopt (p, "verbose")))
        monotime (&t0);

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (flux_get_size (h, &size) < 0)
        log_err_exit ("flux_get_size");
    if (av[n] && !(dirpath = realpath (av[n], NULL)))
        log_err_exit ("%s", av[n]);
    if (!(idset = ranks_to_idset (h, nodeset)))
        log_msg_exit ("--rank=%s: Invalid argument", nodeset);
    if (idset_last (idset) > size - 1)
        log_msg_exit ("--rank=%s: Invalid rank specified", nodeset);

    if (verbose)
        log_msg ("%.3fs: starting HWLOC reload on %ju ranks (%s)",
                 seconds_since (t0), (uintmax_t) idset_count (idset), nodeset);

    if (dirpath) {
        if (verbose)
            log_msg ("%.3fs: starting load of XML from %s",
                     seconds_since (t0), dirpath);

        flux_future_t *f = kvs_load_xml_idset (h, dirpath, idset);
        if (!f || flux_future_get (f, NULL) < 0)
            log_err_exit ("%s: failed to load all XML", dirpath);
        flux_future_destroy (f);
        reload_ranks = NULL;

        if (verbose)
            log_msg ("%.3fs: XML load complete", seconds_since (t0));
    }
    else
        reload_ranks = strdup (nodeset);

    if (verbose)
        log_msg ("%.3fs: executing aggregate-load across all ranks",
                seconds_since (t0));
    run_hwloc_aggregate (h, reload_ranks, optparse_hasopt (p, "verbose"), t0);

    // run_hwloc_aggregate doesn't return, but clean up anyway:
    free (dirpath);
    free (reload_ranks);
    idset_destroy (idset);
    flux_close (h);
    return (0);
}



/*  flux-hwloc aggregate-load:
 */

/*  Count supported GPU object in a hwloc topology object.
 *
 *  Currently only CUDA and OpenCL devices are counted.
 */
static int hwloc_gpu_count (hwloc_topology_t topology)
{
    int nobjs = 0;
    hwloc_obj_t obj = NULL;
    while ((obj = hwloc_get_next_osdev (topology, obj))) {
        /* Only count cudaX and openclX devices for now */
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && ((strcmp (s, "CUDA") == 0) || (strcmp (s, "OpenCL") == 0)))
            nobjs++;
    }
    return (nobjs);
}

/*  Emit a json object containing summary statistics for the topology argument.
 */
static json_t *topo_tojson (hwloc_topology_t topology)
{
    int nobj, i;
    json_t *o = NULL;
    json_t *v = NULL;
    int depth = hwloc_topology_get_depth (topology);

    if (!(o = json_object ()))
        return NULL;
    for (i = 0; i < depth; i++) {
        hwloc_obj_type_t t = hwloc_get_depth_type (topology, i);
        nobj = hwloc_get_nbobjs_by_depth (topology, i);
        /* Skip "Machine" or "System" = 1 */
        if ((t == HWLOC_OBJ_MACHINE || t == HWLOC_OBJ_SYSTEM) && nobj == 1)
            continue;
        if (!(v = json_integer (nobj)))
            goto error;
        if (json_object_set_new (o, hwloc_obj_type_string (t), v) < 0)
            goto error;
    }
    if ((nobj = hwloc_gpu_count (topology))) {
        if (!(v = json_integer (nobj))
            || json_object_set_new (o, "GPU", v) < 0)
            goto error;
    }
    return (o);
error:
    json_decref (o);
    return (NULL);
}

static int get_fwd_count (flux_t *h)
{
    const char *s = flux_attr_get (h, "tbon.descendants");
    long v = strtol (s, NULL, 10);
    if (v >= 0)
        return ((int) v + 1);
    return (0);
}

/*  Create a JSON object summarizing object counts in local topology
 *   and initiate an aggregate across all ranks of that object.
 */
static int aggregate_topo_summary (flux_t *h, const char *key, const char *xml)
{
    json_t *o = NULL;
    flux_future_t *f = NULL;
    hwloc_topology_t topo;
    int fwd_count = get_fwd_count (h);

    if (init_topo_from_xml (&topo, xml) < 0)
        log_err_exit ("aggregate_topo_summary: failed to initialize topology");

    if (!(o = topo_tojson (topo)))
        log_err_exit ("Failed to convert topology to JSON");

    if (!(f = aggregator_push_json (h, fwd_count, 1., key, o))
        || (flux_future_get (f, NULL) < 0))
        log_err_exit ("aggregator_push_json");

    flux_future_destroy (f);
    hwloc_topology_destroy (topo);
    return (0);
}

static void aggregate_load_wait (optparse_t *p, flux_t *h, const char *key)
{
    const char *unpack_path = NULL;
    double timeout;
    flux_future_t *f = NULL;

    timeout = optparse_get_double (p, "timeout", 15.);

    if (!(f = aggregate_wait (h, key))
       || flux_future_wait_for (f, timeout) < 0)
        log_err_exit ("aggregate_wait");

    if (optparse_getopt (p, "unpack", &unpack_path)
        && aggregate_unpack_to_kvs (f, unpack_path) < 0)
        log_err_exit ("unable to unpack aggregate to kvs");

    if (optparse_hasopt (p, "print-result")) {
        const char *s;
        if (aggregate_wait_get (f, &s) < 0)
            log_err_exit ("aggregate_wait_get_unpack");
        puts (s);
    }
    flux_future_destroy (f);
}

/*  Put xml string `xml` into hwloc xml entry in kvs for rank `rank`,
 *   and then perform synchronous kvs_fence for nprocs entries.
 */
static int kvs_put_xml_fence (flux_t *h, int rank,
                              const char *name, int nprocs,
                              const char *xml)
{
    flux_future_t *f;
    flux_kvs_txn_t *txn = NULL;

    if (!(txn = flux_kvs_txn_create ())
        || (kvs_txn_put_xml (txn, rank, xml) < 0))
        log_err_exit ("kvs put xml (rank=%d)", rank);
    if (!(f = flux_kvs_fence (h, NULL, 0, name, nprocs, txn))
        || flux_future_get (f, NULL) < 0)
        log_err_exit ("kvs_put_xml: commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return (0);
}

double gettime (void)
{
    struct timespec t;
    clock_gettime (CLOCK_MONOTONIC, &t);
    return ((double) t.tv_sec + ((double) t.tv_nsec / 1000000000.));
}


/*  Undocumented utility function that optionally loads local topology XML
 *   on selected ranks, then uses the aggregator module to create a
 *   summary of all rank HW topology object types, optionally storing the
 *   result in the KVS.
 */
static int cmd_aggregate_load (optparse_t *p, int ac, char *av[])
{
    char *xml = NULL;
    const char *key = NULL;
    const char *ranks = NULL;
    struct idset *idset = NULL;
    uint32_t rank;
    struct timespec t0;
    bool verbose;
    flux_t *h = NULL;

    log_init ("flux-hwloc aggregate-load");

    if ((verbose = optparse_hasopt (p, "verbose"))) {
        setlinebuf (stderr);
        monotime (&t0);
    }

    if (!optparse_getopt (p, "key", &key))
        log_err_exit ("Missing required option --key");

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    if (flux_get_rank (h, &rank) < 0)
        log_err_exit ("flux_get_rank");

    if (verbose && rank != 0)
        verbose = false;

    /*  If rank not specified then default to an empty set */
    if (optparse_getopt (p, "rank", &ranks) <= 0)
        ranks = "";
    if (!(idset = ranks_to_idset (h, ranks)))
        log_err_exit ("Invalid argument: -rank='%s'", ranks);

    if (verbose)
        log_msg ("%.3fs: %.3fs: starting", gettime (), seconds_since (t0));

    /*  If this rank is in idset, then we need to reload local XML into
     *   kvs before re-aggregation. Otherwise, fetch XML from kvs.
     */
    if (idset_test (idset, rank)) {
        if (verbose)
            log_msg ("%.3fs: %.3fs: pushing local xml", gettime (), seconds_since (t0));
        if (!(xml = flux_hwloc_local_xml ()))
            log_err_exit ("Failed to get local XML");
        if (verbose)
            log_msg ("%.3fs: %.3fs: starting kvs fence", gettime (), seconds_since (t0));
        if (kvs_put_xml_fence (h, rank, key, idset_count (idset), xml) < 0)
            log_err_exit ("Failed to store local XML in kvs");
        if (verbose)
            log_msg ("%.3fs: %.3fs: kvs fence complete", gettime (), seconds_since (t0));
    }
    else if (lookup_one_topo_xml (h, &xml, rank) < 0)
        log_err_exit ("lookup topo XML for this rank (%d)", rank);

    if (verbose)
        log_msg ("%.3fs: %.3fs: starting aggregate", gettime (), seconds_since (t0));

    /* Immediately push aggregate from all ranks
     */
    if (aggregate_topo_summary (h, key, xml) < 0)
        log_err_exit ("Unable to aggregate topology summary");

    if (verbose)
        log_msg ("%.3fs: %.3fs: aggregate push complete", gettime (), seconds_since (t0));

    /*  Rank 0 waits for aggregate completion and optionally "unpacks"
     *   aggregate to new KVS location.
     */
    if (rank == 0)
        aggregate_load_wait (p, h, key);
    if (verbose)
        log_msg ("%.3fs: %.3fs: aggregate_wait complete", gettime (), seconds_since (t0));

    idset_destroy (idset);
    free (xml);
    flux_close (h);
    if (verbose)
        log_msg ("%.3fs: done.", monotime_since (t0)/1000.);
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

static struct optparse_option reload_opts[] = {
    { .name = "verbose",  .key = 'v',  .has_arg = 0,
      .usage = "Increase verbosity", },
    { .name = "rank",  .key = 'r',  .has_arg = 1,
      .usage = "Target specified nodeset, or \"all\" (default)", },
    OPTPARSE_TABLE_END,
};

static struct optparse_option topology_opts[] = {
    { .name = "local", .key = 'l', .has_arg = 0,
      .usage = "Dump topology XML for the local host only",
    },
    { .name = "rank", .key = 'r', .has_arg = 1,
      .usage = "Target specified nodeset, or \"all\" (default)",
    },
    OPTPARSE_TABLE_END,
};

static struct optparse_option aggregate_load_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Increase verbosity (only affects rank 0)",
    },
    { .name = "timeout", .key = 't', .has_arg = 1,
      .usage = "Time to wait for aggregate completion (default 15.0s)",
    },
    { .name = "rank", .key = 'r', .has_arg = 1,
      .usage = "ranks on which to perform a local topology reload",
    },
    { .name = "key", .key = 'k', .has_arg = 1,
      .usage = "KVS key for aggregate",
    },
    { .name = "unpack", .key = 'u', .has_arg = 1,
      .usage = "KVS key to which to optionally \"unpack\" aggregate",
    },
    { .name = "print-result", .key = 'p', .has_arg = 0,
      .usage = "Print final aggregate on rank 0 upon completion",
    },
    OPTPARSE_TABLE_END,
};

static struct optparse_subcommand hwloc_subcmds[] = {
    { "reload",
      "[OPTIONS] [DIR]",
      "Reload hwloc XML, optionally from DIR/<rank>.xml files",
      internal_hwloc_reload,
      0,
      reload_opts,
    },
    { "lstopo",
      "[lstopo-OPTIONS]",
      "Show hwloc topology of the system",
      cmd_lstopo,
      OPTPARSE_SUBCMD_SKIP_OPTS,
      NULL,
    },
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
    { "aggregate-load",
      "[OPTIONS]",
      "aggregate hwloc summary with optional local topology reload",
      cmd_aggregate_load,
      OPTPARSE_SUBCMD_HIDDEN,
      aggregate_load_opts,
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
