/* flux-kvs.c - flux kvs subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hdD"
static const struct option longopts[] = {
    {"help",       no_argument,  0, 'h'},
    {"dropcache",  no_argument,  0, 'd'},
    {"dropcache-all",  no_argument,  0, 'D'},
    { 0, 0, 0, 0 },
};

void get (flux_t h, const char *key);
void del (flux_t h, const char *key);
void put (flux_t h, const char *key, const char *val);

void usage (void)
{
    fprintf (stderr, "Usage: flux-kvs key[=val] [key[=val]] ...\n"
"where the arguments are one or more of:\n"
"    key         displays value of key\n"
"    key=        unlinks key\n"
"    key=val     sets value of key (with commit)\n"
"and 'val' has the form:\n"
"    4           json int\n"
"    4.2         json double\n"
"    true|false  json boolean\n"
"    [1,2,3]     json array (of int, but may be any type)\n"
"    \"string\"    json string\n"
"    {...}       json object\n"
"remember to escape any characters that are interpted by your shell\n"
"Use --dropcache to drop the local slave cache\n."
"Use --dropcache-all to drop slave caches across the session\n."
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    bool need_commit = false;
    int i, ch;
    bool dopt = false;
    bool Dopt = false;

    log_init ("flux-kvs");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'd': /* --dropcache */
                dopt = true;
                break;
            case 'D': /* --dropcache-all */
                Dopt = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc && !(dopt || Dopt))
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (dopt) {
        if (kvs_dropcache (h) < 0)
            err_exit ("kvs_dropcache");
    }
    if (Dopt) {
        if (flux_event_send (h, NULL, "event.kvs.dropcache") < 0)
            err_exit ("flux_event_send");
    }

    for (i = optind; i < argc; i++) {
        char *key = xstrdup (argv[i]);
        char *val = strchr (key, '=');

        if (val) {
            *val++ = '\0';
            if (*val)
                put (h, key, val);
            else
                del (h, key);
            need_commit = true;
        } else {
            get (h, key);
        free (key);
        }
    }
    if (need_commit) {
        if (kvs_commit (h) < 0)
            err_exit ("kvs_commit");
    }

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

void get (flux_t h, const char *key)
{
    json_object *val;

    if (kvs_get (h, key, &val) < 0) {
        err ("%s", key);
        return;
    }
    printf ("%s=%s\n", key, json_object_to_json_string (val));
    json_object_put (val);
}

void put (flux_t h, const char *key, const char *valstr)
{
    json_object *val = NULL;

    assert (valstr != NULL);
    val = json_tokener_parse (valstr);
    if (kvs_put (h, key, val) < 0)
        err ("%s", key);
    else
        printf ("%s=%s\n", key, json_object_to_json_string (val));
    if (val)
        json_object_put (val);
}

void del (flux_t h, const char *key)
{
    if (kvs_unlink (h, key) < 0)
        err ("%s", key);
    else
        printf ("%s=\n", key);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
