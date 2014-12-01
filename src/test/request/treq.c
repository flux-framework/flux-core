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
#include <getopt.h>
#include <json.h>
#include <assert.h>
#include <libgen.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

void test_null (flux_t h, uint32_t nodeid);
void test_echo (flux_t h, uint32_t nodeid);
void test_err (flux_t h, uint32_t nodeid);
void test_src (flux_t h, uint32_t nodeid);

typedef struct {
    const char *name;
    void (*fun)(flux_t h, uint32_t nodeid);
} test_t;

static test_t tests[] = {
    { "null", &test_null },
    { "echo", &test_echo },
    { "err", &test_err},
    { "src", &test_src},
};

test_t *test_lookup (const char *name)
{
    int i;
    for (i = 0; i < sizeof (tests) / sizeof (tests[0]); i++)
        if (!strcmp (tests[i].name, name))
            return &tests[i];
    return NULL;
}

#define OPTIONS "hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: treq [--rank N] {null | echo | err | src}\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    uint32_t nodeid = FLUX_NODEID_ANY;
    test_t *t;

    log_init ("treq");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank N */
                nodeid = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    if (!(t = test_lookup (argv[optind])))
        usage ();

    if (!(h = flux_api_open ()))
        err_exit ("flux_api_open");

    t->fun (h, nodeid);

    flux_api_close (h);

    log_fini ();
    return 0;
}

void test_null (flux_t h, uint32_t nodeid)
{
    if (flux_json_rpc (h, nodeid, "req.null", NULL, NULL) < 0)
        err_exit ("req.null");
}

void test_echo (flux_t h, uint32_t nodeid)
{
    JSON in = Jnew ();
    JSON out = NULL;
    const char *s;

    Jadd_str (in, "mumble", "burble");
    if (flux_json_rpc (h, nodeid, "req.echo", in, &out) < 0)
        err_exit ("%s", __FUNCTION__);
    if (!out)
        msg_exit ("%s: no JSON returned", __FUNCTION__);
    if (!Jget_str (out, "mumble", &s) || strcmp (s, "burble") != 0)
        msg_exit ("%s: returned JSON wasn't an echo", __FUNCTION__);
    Jput (in);
    Jput (out);
}

void test_err (flux_t h, uint32_t nodeid)
{
    if (flux_json_rpc (h, nodeid, "req.err", NULL, NULL) == 0)
        msg_exit ("%s: succeeded when should've failed", __FUNCTION__);
    if (errno != 42)
        msg_exit ("%s: got errno %d instead of 42", __FUNCTION__, errno);
}

void test_src (flux_t h, uint32_t nodeid)
{
    JSON out = NULL;
    int i;
    if (flux_json_rpc (h, nodeid, "req.src", NULL, &out) < 0)
        err_exit ("%s", __FUNCTION__);
    if (!out)
        msg_exit ("%s: no JSON returned", __FUNCTION__);
    if (!Jget_int (out, "wormz", &i) || i != 42)
        msg_exit ("%s: didn't get expected JSON", __FUNCTION__);
    Jput (out);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
