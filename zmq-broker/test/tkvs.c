/* tkvs.c - exercise basic kvs functions */

/* Usage: tkvs [OPTIONS] get[_type]|put[_type]|commit key [val] */

#include <sys/types.h>
#include <sys/time.h>
#include <json/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <zmq.h>
#include <czmq.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "zmsg.h"

#define OPTIONS "dZw"
static const struct option longopts[] = {
   {"deep",          no_argument,        0, 'd'},
   {"trace-apisock", no_argument,        0, 'Z'},
   {"watch",         no_argument,        0, 'w'},
   {0, 0, 0, 0},
};


static void usage (void)
{
    fprintf (stderr, "%s", "\
Usage: tkvs OPTIONS op [key] [val]\n\
\n\
Where OPTIONS can be one of\n\
    -Z,--trace-apisock\n\
    -w,--watch\n\
    -d,--deep\n\
The possible operations are:\n\
    get key\n\
    put key val\n\
    get_string key\n\
    put_string key val\n\
    get_int key\n\
    put_int key val\n\
    get_int64 key\n\
    put_int64 key val\n\
    get_double key\n\
    put_double key val\n\
    get_boolean key\n\
    put_booelan key val (use \"true\" or \"false\")\n\
    get_dir key\n\
    commit\n\
");
    exit (1);
}

void get (cmb_t c, char *key, bool watch)
{
    int flags = 0;
    json_object *o;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get (c, key, &o, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get %s", key);
        } else {
            printf ("%s\n", json_object_to_json_string (o));
            json_object_put (o);
        }
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void put (cmb_t c, char *key, char *val)
{
    json_object *o;

    if (!(o = json_tokener_parse (val)))
        msg_exit ("error parsing json value");
    if (cmb_kvs_put (c, key, o) < 0)
        err_exit ("cmb_kvs_put %s=%s", key, val);
    json_object_put (o);
}

void get_dir (cmb_t c, char *key, bool watch, bool deep)
{
    int dirflags = KVS_GET_DIRECTORY | (deep ? KVS_GET_DEEP : 0);
    int flags = 0;
    json_object *dir;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get (c, key, &dir, dirflags | flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get %s", key);
        } else {
            printf ("%s\n", json_object_to_json_string (dir)); 
            json_object_put (dir);
        }
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void get_string (cmb_t c, char *key, bool watch)
{
    char *val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_string (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_string %s", key);
        } else {
            printf ("%s\n", val);
            free (val);
        }
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void put_string (cmb_t c, char *key, char *val)
{
    if (cmb_kvs_put_string (c, key, val) < 0)
        err_exit ("cmb_kvs_put_string %s=%s", key, val);
}

void get_int (cmb_t c, char *key, bool watch)
{
    int val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_int (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_int %s", key);
        } else
            printf ("%d\n", val);
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void put_int (cmb_t c, char *key, int val)
{
    if (cmb_kvs_put_int (c, key, val) < 0)
        err_exit ("cmb_kvs_put_int %s=%d", key, val);
}

void get_int64 (cmb_t c, char *key, bool watch)
{
    int64_t val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_int64 (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else 
                err_exit ("cmb_kvs_get_int64 %s", key);
        } else
            printf ("%lld\n", (long long int) val);
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void put_int64 (cmb_t c, char *key, int64_t val)
{
    if (cmb_kvs_put_int64 (c, key, val) < 0)
        err_exit ("cmb_kvs_put_int64 %s=%lld", key, (long long int)val);
}

void get_double  (cmb_t c, char *key, bool watch)
{
    double val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do {
        if (cmb_kvs_get_double (c, key, &val, flags) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_double  %s", key);
        } else
            printf ("%f\n", val);
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void put_double (cmb_t c, char *key, double val)
{
    if (cmb_kvs_put_double (c, key, val) < 0)
        err_exit ("cmb_kvs_put_double %s=%lf", key, val);
}

void get_boolean (cmb_t c, char *key, bool watch)
{
    bool val;
    int flags = 0;

    if (watch)
        flags = KVS_GET_WATCH;
    do { 
        if (cmb_kvs_get_boolean (c, key, &val, 0) < 0) {
            if (errno == ENOENT)
                printf ("null\n");
            else
                err_exit ("cmb_kvs_get_boolean %s", key);
        } else
            printf ("%s\n", val ? "true" : "false");
        if (watch)
            flags = KVS_GET_NEXT;
    } while (watch);
}

void put_boolean (cmb_t c, char *key, bool val)
{
    if (cmb_kvs_put_boolean (c, key, val) < 0)
        err_exit ("cmb_kvs_put_boolean %s=%s", key, val ? "true" : "false");
}
void commit (cmb_t c)
{
    if (cmb_kvs_commit (c) < 0)
        err_exit ("cmb_kvs_commit");
}

int main (int argc, char *argv[])
{
    int ch;
    bool dopt = false;
    bool wopt = false;
    char *op = NULL;
    char *key = NULL;
    char *val = NULL;
    cmb_t c;
    char path[PATH_MAX + 1];
    int flags = 0;

    log_init (basename (argv[0]));
    snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'd':
                dopt = true;
                break;
            case 'Z': /* --trace-apisock */
                flags |= CMB_FLAGS_TRACE;
                break;
            case 'w': /* --watch */
                wopt = true;
                break;
            default:
                usage ();
        }
    }
    if (optind < argc)
        op = argv[optind++];
    if (optind < argc)
        key = argv[optind++];
    if (optind < argc)
        val = argv[optind++];
    if (!op || (!strncmp (op, "get", 3) && !key)
            || (!strncmp (op, "set", 3) && (!key || !val)))
        usage ();
    if (!(c = cmb_init_full (path, flags)))
        err_exit ("cmb_init");

    if (!strcmp (op, "get_string"))
        get_string (c, key, wopt);
    else if (!strcmp (op, "put_string"))
        put_string (c, key, val);

    else if (!strcmp (op, "get_int"))
        get_int (c, key, wopt);
    else if (!strcmp (op, "put_int"))
        put_int (c, key, strtoul (val, NULL, 10));

    else if (!strcmp (op, "get_int64"))
        get_int64 (c, key, wopt);
    else if (!strcmp (op, "put_int64"))
        put_int64 (c, key, strtoull (val, NULL, 10));

    else if (!strcmp (op, "get_double"))
        get_double (c, key, wopt);
    else if (!strcmp (op, "put_double"))
        put_double (c, key, strtod (val, NULL));

    else if (!strcmp (op, "get_boolean"))
        get_boolean (c, key, wopt);
    else if (!strcmp (op, "put_boolean"))
        put_boolean (c, key, !strcmp (val, "false") ? false : true);

    else if (!strcmp (op, "get_dir"))
        get_dir (c, key, wopt, dopt);

    else if (!strcmp (op, "get"))
        get (c, key, wopt);
    else if (!strcmp (op, "put"))
        put (c, key, val);

    else if (!strcmp (op, "commit"))
        commit (c);     
    else
        usage ();
    cmb_fini (c);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
