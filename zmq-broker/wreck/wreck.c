#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <json/json.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <zmq.h>
#include <czmq.h>

#include "optparse.h"
#include "cmb.h"
#include "log.h"
#include "util.h"
#include "zmsg.h"

struct optparse_option options [] = {
    { "nprocs", 'n', 1, 1, "N",
        "Set number of procs per node = N", 0, 0 },
    { "create-jobid", 'c', 0, 1, NULL,
        "Create new jobid only. Don't fill in any job information", 0, 0 },
    OPTPARSE_TABLE_END,
};

static int64_t process_reply (cmb_t c)
{
    int64_t jobid;
    char *tag;
    zmsg_t *zmsg;
    json_object *o = NULL;

    if (!(zmsg = cmb_recv_zmsg (c, false))) {
        fprintf (stderr, "Failed to recv zmsg!\n");
        exit (1);
    }

    if (cmb_msg_decode (zmsg, &tag, &o) < 0) {
        fprintf (stderr, "cmb_msg_decode failed!\n");
        exit (1);
    }

    if (util_json_object_get_int64 (o, "jobid", &jobid) < 0) {
        err_exit ("failed to get jobid from json = '%s'",
                    json_object_to_json_string (o));
    }

    json_object_put (o);
    zmsg_destroy (&zmsg);
    return (jobid);
}

struct json_object *argv_to_json (int ac, char **av)
{
    int i;
    struct json_object *o = json_object_new_array ();
    for (i = 0; i < ac; i++) {
        json_object *ox = json_object_new_string (av[i]);
        json_object_array_add (o, ox);
    }
    return (o);
}

optparse_t process_cmdline (const char *prog, int ac, char **av, int *pind)
{
    optparse_t p;
    optparse_err_t err;
    int optind;

    if (!(p = optparse_create (prog)))
        err_exit ("Failed to create options handler");

    optparse_set (p, OPTPARSE_USAGE, "[OPTIONS]... [COMMAND]...");

    err = optparse_add_doc (p, "Register lightweight jobs in CMB KVS", -1);
    if (err != OPTPARSE_SUCCESS)
        err_exit ("Failed to create options doc");

    err = optparse_add_option_table (p, options);
    if (err != OPTPARSE_SUCCESS)
        err_exit ("Failed to register options");

    optind = optparse_parse_args (p, ac, av);
    if (optind < 0)
        err_exit ("Failed to parse args");

    *pind = optind;
    return (p);
}

int get_int (const char *arg)
{
    char *p;
    long l = strtol (arg, &p, 10);
    if ((l == LONG_MIN) || (l == LONG_MAX) || (*p != '\0'))
        err_exit ("Invalid argument '%s'", arg);
    return ((int) l);
}

int main (int ac, char **av)
{
    cmb_t c;
    optparse_t p;
    json_object *jobreq;
    const char *optarg;
    bool create_only = false;
    int nprocs = 1;
    const char *progname = basename (av[0]);

    p = process_cmdline (progname, ac, av, &optind);

    if (optparse_getopt (p, "create-jobid", NULL))
        create_only = true;

    if (optparse_getopt (p, "nprocs", &optarg)) {
        if (create_only)
            err_exit ("Do not specify any other options with --create-jobid");
        if ((nprocs = get_int (optarg)) <= 0)
            err_exit ("Invalid argument: --nprocs='%s'", optarg);
    }

    if (!(c = cmb_init ())) {
        fprintf (stderr, "Failed to open connection to cmb!\n");
        exit (1);
    }

    jobreq = json_object_new_object ();
    if (!create_only) {
        if ((ac - optind) <= 0)
            err_exit ("Usage: %s [OPTIONS]... [COMMAND]...", progname);
        util_json_object_add_int (jobreq, "nprocs", nprocs);
        json_object_object_add (jobreq, "cmdline",
                argv_to_json (ac-optind, &av[optind]));
    }

    if (cmb_send_message (c, jobreq, "job.create") < 0) {
        fprintf (stderr, "cmb_send_message failed!\n");
        exit (1);
    }

    fprintf (stdout, "%lu\n", process_reply (c));

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
