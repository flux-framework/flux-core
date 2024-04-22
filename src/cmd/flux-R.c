/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-R - encode/decode and operate on RFC 20 resource set objects
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/hostlist.h>
#include <flux/optparse.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/librlist/rlist.h"
#include "src/common/librlist/rhwloc.h"
#include "src/common/libtomlc99/toml.h"
#include "ccan/str/str.h"

#define RSET_DOC "\
Read, generate, and process RFC 20 Resource Set objects.\n\
Options:"

int cmd_encode (optparse_t *p, int argc, char **argv);
int cmd_append (optparse_t *p, int argc, char **argv);
int cmd_diff (optparse_t *p, int argc, char **argv);
int cmd_intersect (optparse_t *p, int argc, char **argv);
int cmd_remap (optparse_t *p, int argc, char **argv);
int cmd_rerank (optparse_t *p, int argc, char **argv);
int cmd_decode (optparse_t *p, int argc, char **argv);
int cmd_verify (optparse_t *p, int argc, char **argv);
int cmd_set_property (optparse_t *p, int argc, char **argv);
int cmd_parse_config (optparse_t *p, int argc, char **argv);

static struct optparse_option global_opts[] =  {
    OPTPARSE_TABLE_END
};

static struct optparse_option encode_opts[] = {
    { .name = "ranks", .key = 'r',
      .has_arg = 1, .arginfo = "IDSET",
      .usage = "Generate an R with ranks in IDSET. If not provided then "
               "either match the number of nodes given  in --hosts option, "
               "or emit a single rank: \"0\"",
    },
    { .name = "cores", .key = 'c',
      .has_arg = 1, .arginfo = "IDS",
      .usage = "Assign cores with IDS to each rank in R. Default is to "
               "assign a single core \"0\" to each rank.",
    },
    { .name = "gpus", .key = 'g',
      .has_arg = 1, .arginfo = "IDS",
      .usage = "Assign gpu resources with IDS to each rank in R. Default "
               "is to assign no gpu resources.",
    },
    { .name = "hosts", .key = 'H',
      .has_arg = 1, .arginfo = "HOSTS",
      .usage = "Generate R with nodelist set to HOSTS. By default, duplicate "
               "the local hostname to match the number of ranks given in "
               "--ranks.",
    },
    { .name = "property", .key = 'p',
      .has_arg = 1, .arginfo = "NAME[:RANKS]",
      .usage = "Assign property NAME to target ranks RANKS. If RANKS is not "
               "specified then the property applies to all defined ranks. "
               "This option may be specified multiple times for each property",
    },
    { .name = "local", .key = 'l',
      .has_arg = 0,
      .usage = "Generate child resources from local system",
    },
    { .name = "xml", .key = 'f',
      .has_arg = 1,
      .usage = "Generate child resources from hwloc XML",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option append_opts[] = {
    OPTPARSE_TABLE_END
};

static struct optparse_option diff_opts[] = {
    OPTPARSE_TABLE_END
};

static struct optparse_option intersect_opts[] = {
    OPTPARSE_TABLE_END
};

static struct optparse_option remap_opts[] = {
    OPTPARSE_TABLE_END
};

static struct optparse_option rerank_opts[] = {
    OPTPARSE_TABLE_END
};

static struct optparse_option verify_opts[] = {
    OPTPARSE_TABLE_END
};

static struct optparse_option set_property_opts[] = {
    OPTPARSE_TABLE_END
};

static struct optparse_option decode_opts[] = {
    { .name = "short", .key = 's',
      .usage = "Print short-form representation of R"
    },
    { .name = "nodelist", .key = 'n',
      .usage = "Print nodelist in hostlist form from R, if any"
    },
    { .name = "ranks", .key = 'r',
      .usage = "Print ranks in idset form from R, if any"
    },
    { .name = "count", .key = 'c',
      .has_arg = 1, .arginfo = "TYPE",
      .usage = "Print count of resource TYPE in R (ranks, core, gpu)",
    },
    { .name = "include", .key = 'i',
      .has_arg = 1, .arginfo = "RANKS",
      .usage = "Include only specified ranks.",
    },
    { .name = "exclude", .key = 'x',
      .has_arg = 1, .arginfo = "RANKS",
      .usage = "Exclude specified ranks.",
    },
    { .name = "properties", .key = 'p',
      .has_arg = 1, .arginfo = "LIST",
      .usage = "Filter on properties"
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option parse_config_opts[] = {
    OPTPARSE_TABLE_END
};


static struct optparse_subcommand subcommands[] = {
    { "encode",
      "[OPTIONS]...",
      "\nEncode RFC 20 R objects for testing.\n\nOptions:\n",
      cmd_encode,
      0,
      encode_opts,
    },
    { "append",
      "",
      "Append multiple R objects on stdin. "
      "Emits an error if resource sets are not disjoint.",
      cmd_append,
      0,
      append_opts,
    },
    { "diff",
      "",
      "Return the set difference of multiple R objects on stdin. "
      "(i.e. (R1 - R2) - R3 ...)",
      cmd_diff,
      0,
      diff_opts,
    },
    { "intersect",
      "",
      "Return the intersection of all R objects on stdin",
      cmd_intersect,
      0,
      intersect_opts,
    },
    { "remap",
      "",
      "Return the union of all R objects on stdin with ranks re-numbered "
      "starting from index 0.",
      cmd_remap,
      0,
      remap_opts,
    },
    { "rerank",
      "HOSTLIST",
      "Return the union of all R objects on stdin with ranks re-mapped "
      " based on their index in HOSTLIST.",
      cmd_rerank,
      0,
      rerank_opts,
    },
    { "decode",
      "[OPTIONS]...",
      "\nReturn the union of all R objects on stdin and print details or "
      "summary of the result. By default an RFC 20 JSON object is emitted "
      "on stdout, unless one or more options below are used\n"
      "\nOptions:\n",
      cmd_decode,
      0,
      decode_opts,
    },
    { "verify",
      "",
      "Takes 2 R objects on stdin and verifies that the resources in each "
      "rank present in R2 has at least resources present for the same rank "
      "in R1.",
      cmd_verify,
      0,
      verify_opts,
    },
    { "set-property",
      "PROPERTY:RANKS [PROPERTY:RANKS]...",
      "Set properties on R object on stdin, emitting the result on stdout",
      cmd_set_property,
      0,
      set_property_opts,
    },
    { "parse-config",
      "PATH",
      "Read config from resource.config array",
      cmd_parse_config,
      0,
      parse_config_opts,
    },
    OPTPARSE_SUBCMD_END
};

int main (int argc, char *argv[])
{
    optparse_t *p;
    int optindex;
    int exitval;

    log_init ("flux-R");

    p = optparse_create ("flux-R");

    if (optparse_add_option_table (p, global_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table() failed");

    if (optparse_add_doc (p, RSET_DOC, 0) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_doc failed");

    if (optparse_reg_subcommands (p, subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_reg_subcommands");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    if ((argc - optindex == 0)
        || !optparse_get_subcommand (p, argv[optindex]))
        optparse_fatal_usage (p, 1, NULL);

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

static struct idset * idset_from_option (optparse_t *p,
                                         const char *name,
                                         const char *dflt)
{
    const char *s = NULL;
    struct idset *result = NULL;

    if (optparse_getopt (p, name, &s) == 0)
        s = dflt;
    if (!(result = idset_decode (s)))
        log_msg_exit ("Failed to decode %s='%s' as idset", name, s);
    return result;
}

static struct hostlist *hostlist_from_option (optparse_t *p,
                                              const char *name,
                                              int expected_count)
{
    const char *s;
    struct hostlist *hl;
    if (optparse_getopt (p, name, &s) == 0) {
        char host[1024];
        if (gethostname (host, sizeof (host)) < 0)
            log_err_exit ("gethostname");

        hl = hostlist_create ();
        for (int i = 0; i < expected_count; i++)
            hostlist_append (hl, host);
        return hl;
    }
    if (!(hl = hostlist_decode (s)))
        log_msg_exit ("invalid hostlist '%s'", s);
    if (expected_count && hostlist_count (hl) != expected_count)
        log_msg_exit ("hostname count in '%s' does not match nranks (%d)",
                      s, expected_count);
    return hl;
}

static void rlist_puts (struct rlist *rl)
{
    char *R = rlist_encode (rl);
    puts (R);
    free (R);
}

static struct idset *idset_from_count (int n)
{
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);
    if (ids) {
        for (int i = 0; i < n; i++)
            idset_set (ids, i);
    }
    return ids;
}

static void get_ranks_and_hostlist (optparse_t *p,
                                    struct idset **ranksp,
                                    struct hostlist **hlp)
{
    if (!optparse_hasopt (p, "ranks")) {
        /*  --ranks not provided, either build from provided hostlist or
         *   emit a single rank.
         */
        if (optparse_hasopt (p, "hosts"))
            *hlp = hostlist_from_option (p, "hosts", 0);
        else
            *hlp = hostlist_from_option (p, "hosts", 1);
        *ranksp = idset_from_count (hostlist_count (*hlp));
    }
    else {
        *ranksp = idset_from_option (p, "ranks", "");
        *hlp = hostlist_from_option (p, "hosts", idset_count (*ranksp));
    }
}

static ssize_t fread_all (const char *path, char **bufp)
{
    int fd;
    ssize_t size;
    void *buf;

    if (streq (path, "-"))
        fd = STDIN_FILENO;
    else {
        if ((fd = open (path, O_RDONLY)) < 0)
            log_err_exit ("%s", path);
    }
    if ((size = read_all (fd, &buf)) < 0)
        log_err_exit ("%s", path);
    if (fd != STDIN_FILENO)
        (void)close (fd);
    *bufp = buf;
    return size;
}

static char *get_xml (optparse_t *p)
{
    const char *path = NULL;
    char *s = NULL;

    /*  If --xml option provided, then read XML file and return contents
     *  O/w, if --local provided, then retern XML from local topology
     *   (this allows the program to gather hwloc topology only once)
     */
    if (optparse_getopt (p, "xml", &path) > 0) {
        if (fread_all (path, &s) < 0)
            log_err_exit ("failed to read XML");
    }
    else if (optparse_hasopt (p, "local")) {
        if (!(s = rhwloc_local_topology_xml (0)))
            log_err_exit ("failed to gather local topology XML");
    }

    return s;
}

static char *rlist_ranks_string (struct rlist *rl)
{
    char *s = NULL;
    struct idset *ranks = rlist_ranks (rl);
    if (ranks)
        s = idset_encode (ranks, IDSET_FLAG_RANGE);
    idset_destroy (ranks);
    return s;
}

static void set_one_property (json_t *o, char *allranks, const char *s)
{
    json_t *val;
    char *ranks;
    char *property;

    if (!(property = strdup (s)))
        log_err_exit ("get_properties: strdup");
    if ((ranks = strchr (property, ':')))
        *ranks++ = '\0';
    else
        ranks = allranks;
    if (!(val = json_string (ranks))
            || json_object_set_new (o, property, val) < 0)
        log_err_exit ("failed to set property %s=%s", property, ranks);

    free (property);
}

static void set_properties (optparse_t *p, struct rlist *rl)
{
    const char *s;
    char *allranks;
    json_t *o;
    flux_error_t err;

    if (!optparse_hasopt (p, "property"))
        return;

    if (!(o = json_object ()))
        log_err_exit ("failed to create properties JSON object");

    if (!(allranks = rlist_ranks_string (rl)))
        log_err_exit ("failed to get rank idset string");

    optparse_getopt_iterator_reset (p, "property");
    while ((s = optparse_getopt_next (p, "property")))
        set_one_property (o, allranks, s);

    if (rlist_assign_properties (rl, o, &err) < 0)
        log_msg_exit ("failed to assign properties: %s", err.text);

    json_decref (o);
    free (allranks);
}

int cmd_encode (optparse_t *p, int argc, char **argv)
{
    struct hostlist *hl;
    struct idset *ranks;
    struct rlist *rl;
    char *xml = NULL;

    unsigned int i;
    const char *host;
    const char *cores;
    const char *gpus;

    gpus = optparse_get_str (p, "gpus", "");
    cores = optparse_get_str (p, "cores", "");

    /* If neither cores nor gpus were set for these ranks, default
     *  to a single coreid 0
     */
    if (strlen (gpus) == 0 && strlen (cores) == 0)
        cores = "0";
    else if (optparse_hasopt (p, "local") || optparse_hasopt (p, "xml"))
        log_msg_exit ("do not specify --cores or --gpus with --local or --xml");

    get_ranks_and_hostlist (p, &ranks, &hl);
    xml = get_xml (p);

    if (!(rl = rlist_create ()))
        log_err_exit ("rlist_create failed");

    i = idset_first (ranks);
    host = hostlist_first (hl);

    while (i != IDSET_INVALID_ID) {
    struct rlist *rloc = NULL;
        if (optparse_hasopt (p, "local") || xml != NULL) {
            if (!(rloc = rlist_from_hwloc (i, xml)))
                log_err_exit ("rlist_from_hwloc");
            if (rlist_assign_hosts (rloc, host) < 0)
                log_err_exit ("rlist_assign_hosts (%s)", host);
            if (rlist_append (rl, rloc) < 0)
                log_err_exit ("rlist_append");
            rlist_destroy (rloc);
        }
        else if (rlist_append_rank_cores (rl, host, i, cores) < 0)
            log_err_exit ("rlist_append rank=%u", i);
        if (strlen (gpus))
            rlist_rank_add_child (rl, i, "gpu", gpus);
        i = idset_next (ranks, i);
        host = hostlist_next (hl);
    }

    set_properties (p, rl);

    rlist_puts (rl);

    free (xml);
    idset_destroy (ranks);
    hostlist_destroy (hl);
    rlist_destroy (rl);

    return 0;
}

static void rlist_freefn (void **x)
{
    if (x) {
        rlist_destroy ((struct rlist *) *x);
        *x = NULL;
    }
}

/*  Load a list of R objects from stream 'fp'.
 */
static zlistx_t * rlist_loadf (FILE *fp)
{
    json_t *o;
    json_error_t err;
    struct rlist *rl;
    zlistx_t *l;

    if (!(l = zlistx_new ()))
        log_err_exit ("zlistx_new");
    zlistx_set_destructor (l, rlist_freefn);

    while ((o = json_loadf (stdin, JSON_DISABLE_EOF_CHECK, &err))) {
        if (!(rl = rlist_from_json (o, &err)))
            log_msg_exit ("Failed to decode R on stdin: %s", err.text);
        json_decref (o);
        zlistx_add_end (l, rl);
    }
    if (zlistx_size (l) == 0)
        log_msg_exit ("Failed to read an R object: %s", err.text);
    return l;
}

static struct rlist * rl_append_all (FILE *fp)
{
    struct rlist *result;
    struct rlist *rl;
    zlistx_t *l = rlist_loadf (fp);
    if (l == NULL)
        log_err_exit ("rlist_loadf()");

    if (!(result = rlist_create ()))
        log_err_exit ("rlist_create");

    rl = zlistx_first (l);
    while (rl) {
        struct rlist *intersect = rlist_intersect (result, rl);
        if (rlist_nnodes (intersect))
            log_msg_exit ("R objects '%s' and '%s' overlap",
                          rlist_dumps (result),
                          rlist_dumps (rl));
        if (rlist_append (result, rl) < 0)
            log_err_exit ("rlist_append");
        rl = zlistx_next (l);
    }
    zlistx_destroy (&l);
    return result;
}


typedef struct rlist * (*rlist_transform_f) (const struct rlist *a,
                                             const struct rlist *b);

static struct rlist *rl_transform (const char *cmd,
                                   FILE *fp,
                                   int min_sets,
                                   rlist_transform_f fn)
{
    struct rlist *rlprev;
    struct rlist *rl;
    struct rlist *result = NULL;

    zlistx_t *l = rlist_loadf (fp);
    if (l == NULL)
        log_err_exit ("rlist_loadf()");

    if (zlistx_size (l) < min_sets)
        log_msg_exit ("%s requires at least %d resource sets", cmd, min_sets);

    /*  Pop the first item off the list as our starting set:
     */
    rlprev = zlistx_first (l);
    zlistx_detach_cur (l);

    rl = zlistx_next (l);
    while (rl) {
        result = (*fn) (rlprev, rl);
        if (result == NULL)
            log_msg_exit ("%s (%s, %s) failed!",
                          cmd,
                          rlist_dumps (rlprev),
                          rlist_dumps (rl));
        rlist_destroy (rlprev);
        rlprev = result;
        rl = zlistx_next (l);
    }
    zlistx_destroy (&l);

    return rlprev;
}

int cmd_append (optparse_t *p, int argc, char **argv)
{
    struct rlist *result = rl_append_all (stdin);
    if (!result)
        log_err_exit ("Failed to append all R objects on stdin");
    rlist_puts (result);
    rlist_destroy (result);
    return 0;
}

int cmd_diff (optparse_t *p, int argc, char **argv)
{
    struct rlist *result = rl_transform ("diff", stdin, 2, rlist_diff);
    if (!result)
        log_err_exit ("Failed to transform R objects on stdin");
    rlist_puts (result);
    rlist_destroy (result);
    return 0;
}

int cmd_intersect (optparse_t *p, int argc, char **argv)
{
    struct rlist *result = rl_transform ("intersect",
                                         stdin,
                                         2,
                                         rlist_intersect);
    if (!result)
        log_err_exit ("Failed to transform R objects on stdin");
    rlist_puts (result);
    rlist_destroy (result);
    return 0;
}

int cmd_remap (optparse_t *p, int argc, char **argv)
{
    struct rlist *rl = rl_transform ("union", stdin, 1, rlist_union);
    if (!rl)
        log_err_exit ("Failed to transform R objects on stdin");
    if (rlist_remap (rl) < 0)
        log_err_exit ("Failed to re-map R");
    rlist_puts (rl);
    rlist_destroy (rl);
    return 0;
}

int cmd_rerank (optparse_t *p, int argc, char **argv)
{
    struct rlist *rl = rl_transform ("union", stdin, 1, rlist_union);
    if (!rl)
        log_err_exit ("Failed to transform R objects on stdin");
    if (argc != 2)
        log_err_exit ("Must provide a hostlist for re-ranking");
    if (rlist_rerank (rl, argv[1], NULL) < 0) {
        if (errno == ENOENT)
            log_msg_exit ("failed to find one or more provided hosts in R");
        else if (errno == EOVERFLOW)
            log_msg_exit ("Too many hosts specified (expected %zu)",
                          rlist_nnodes (rl));
        else if (errno == ENOSPC)
            log_msg_exit ("Too few hosts specified (expected %zu)",
                          rlist_nnodes (rl));
        else
            log_err_exit ("rlist_rerank");
    }
    rlist_puts (rl);
    rlist_destroy (rl);
    return 0;
}

static json_t *property_constraint_create (const char *arg)
{
    char *tok;
    char *p;
    char *s;
    char *cpy = strdup (arg);
    json_t *result = NULL;
    json_t *o = json_array ();


    if (!cpy || !o)
        goto out;

    s = cpy;
    while ((tok = strtok_r (s, ",", &p))) {
        json_t *prop = json_string (tok);
        if (!prop || json_array_append_new (o, prop) != 0)
            log_msg_exit ("Failed to append %s to properties array",
                          tok);
        s = NULL;
    }
    result = json_pack ("{s:o}", "properties", o);
out:
    free (cpy);
    return result;
}

int cmd_decode (optparse_t *p, int argc, char **argv)
{
    int lines = 0;
    char *s;
    const char *type;
    const char *arg;
    struct rlist *rl = rl_transform ("union", stdin, 1, rlist_union);
    if (!rl)
        log_msg_exit ("failed to read R on stdin");

    if (optparse_getopt (p, "properties", &arg) > 0) {
        struct rlist *tmp = rl;
        flux_error_t error;
        json_t *constraint = property_constraint_create (arg);

        if (!(rl = rlist_copy_constraint (tmp, constraint, &error)))
            log_err_exit ("Invalid property constraint: %s", error.text);

        rlist_destroy (tmp);
        json_decref (constraint);
    }
    if (optparse_getopt (p, "include", &arg) > 0) {
        struct rlist *tmp;
        struct idset *ranks = idset_decode (arg);
        if (!ranks)
            log_err_exit ("Invalid ranks option: '%s'", arg);
        tmp = rl;
        if (!(rl = rlist_copy_ranks (tmp, ranks)))
            log_err_exit ("rlist_copy_ranks(%s) failed", arg);
        rlist_destroy (tmp);
        idset_destroy (ranks);
    }
    if (optparse_getopt (p, "exclude", &arg) > 0) {
        struct idset *ranks = idset_decode (arg);
        if (!ranks)
            log_err_exit ("Invalid idset in --exclude option: %s", arg);
        if (rlist_remove_ranks (rl, ranks) < 0)
            log_err_exit ("error removing ranks %s from R", arg);
        idset_destroy (ranks);
    }
    if (optparse_hasopt (p, "short")) {
        if (!(s = rlist_dumps (rl)))
            log_err_exit ("rlist_dumps");
        printf ("%s\n", s);
        free (s);
        lines++;
    }
    if (optparse_hasopt (p, "nodelist")) {
        struct hostlist *hl = rlist_nodelist (rl);
        if (!hl)
            log_err_exit ("rlist_nodelist");
        if (!(s = hostlist_encode (hl)))
            log_err_exit ("hostlist_encode");
        printf ("%s\n", s);
        free (s);
        hostlist_destroy (hl);
        lines++;
    }
    if (optparse_hasopt (p, "ranks")) {
        struct idset *ids = rlist_ranks (rl);
        if (!ids)
            log_err_exit ("rlist_ranks");
        if (!(s = idset_encode (ids, IDSET_FLAG_RANGE)))
            log_err_exit ("idset_encode");
        printf ("%s\n", s);
        free (s);
        idset_destroy (ids);
        lines++;
    }
    if (optparse_getopt (p, "count", &type)) {
        int count;
        if (streq (type, "node"))
            count = rlist_nnodes (rl);
        else if (streq (type, "core"))
            count = rl->avail;
        else
            count = rlist_count (rl, type);
        printf ("%d\n", count);
        lines++;
    }
    if (lines == 0)
        rlist_puts (rl);
    rlist_destroy (rl);
    return 0;
}


int cmd_verify (optparse_t *p, int argc, char **argv)
{
    int rc;
    flux_error_t error;
    struct rlist *expected;
    struct rlist *rl;
    zlistx_t *l = rlist_loadf (stdin);
    if (!l)
        log_err_exit ("rlist_loadf");
    if (zlistx_size (l) != 2)
        log_msg_exit ("verify requires exactly 2 R objects on stdin");
    if (!(expected = zlistx_first (l))
        || !(rl = zlistx_next (l)))
        log_err_exit ("Unexpected error getting first two rlists");

    rc = rlist_verify (&error, expected, rl);
    if (rc != 0)
        log_msg ("%s", error.text);
    zlistx_destroy (&l);
    if (rc < 0)
        exit (1);
    return 0;
}

int cmd_set_property (optparse_t *p, int argc, char **argv)
{
    json_t *o;
    char *allranks;
    flux_error_t err;

    struct rlist *rl = rl_transform ("union", stdin, 1, rlist_union);
    if (!rl)
        log_msg_exit ("failed to read R on stdin");

    if (!(o = json_object ()))
        log_err_exit ("failed to create properties JSON object");

    if (!(allranks = rlist_ranks_string (rl)))
        log_err_exit ("failed to get rank idset string");

    for (int i = 1; i < argc; i++)
        set_one_property (o, allranks, argv[i]);

    if (rlist_assign_properties (rl, o, &err) < 0)
        log_msg_exit ("failed to assign properties: %s", err.text);

    rlist_puts (rl);

    json_decref (o);
    free (allranks);
    rlist_destroy (rl);

    return 0;
}

int cmd_parse_config (optparse_t *p, int argc, char **argv)
{
    flux_error_t error;
    json_t *o = NULL;
    const char *path = NULL;
    struct rlist *rl = NULL;
    flux_conf_t *conf;

    if (!(conf = flux_conf_parse (argv[1], &error)))
        log_msg_exit ("flux_conf_parse: %s", error.text);

    if (flux_conf_unpack (conf, &error,
                          "{s:{s?o s?s}}",
                          "resource",
                            "config", &o,
                            "path", &path) < 0)
        log_msg_exit ("Config file error: %s", error.text);

    if (!o) {
        json_error_t e;

        if (!path) {
            log_msg_exit ("Config file error:"
                          " resource.config or resource.path must be defined");
        }
        if (!(o = json_load_file (path, 0, &e))
            || !(rl = rlist_from_json (o, &e))) {
            if (e.line == -1)
                log_msg_exit ("%s: %s", path, e.text);
            log_msg_exit ("%s: %s on line %d", path, e.text, e.line);
        }
        json_decref (o);
    }
    else {
        if (!(rl = rlist_from_config (o, &error)))
            log_msg_exit ("Config file error: %s", error.text);
    }

    rlist_puts (rl);

    flux_conf_decref (conf);
    rlist_destroy (rl);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
