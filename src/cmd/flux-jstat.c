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
#include <json.h>
#include <stdio.h>
#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
//#include "src/modules/libjsc/jstatctl.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"


/******************************************************************************
 *                                                                            *
 *                Internal types, macros and static variables                 *
 *                                                                            * 
 ******************************************************************************/
typedef struct {
    flux_t h;
    FILE *op;
} jstatctx_t;

/* Note: Should only be used for signal handling */
static flux_t sig_flux_h;

#define OPTIONS "o:h"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"testout",    required_argument,  0, 'o'},
    { 0, 0, 0, 0 },
};


/******************************************************************************
 *                                                                            *
 *                              Utilities                                     *
 *                                                                            * 
 ******************************************************************************/
static void usage (void)
{
    fprintf (stderr,
"Usage: flux-jstat notify\n"
"       flux-jstat query jobid <top-level JCB attribute>\n"
"       flux-jstat update jobid <top-level JCB attribute> <JCB JSON>\n"
);
    exit (1);
}

static void freectx (void *arg)
{
    jstatctx_t *ctx = arg;
    if (ctx->op)
        fclose (ctx->op);
}

static jstatctx_t *getctx (flux_t h)
{
    jstatctx_t *ctx = (jstatctx_t *)flux_aux_get (h, "jstat");
    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        ctx->op = NULL;
        flux_aux_set (h, "jstat", ctx, freectx);
    }
    return ctx;
}

static void sig_handler (int s)
{
    if (s == SIGINT) {
        fprintf (stdout, "Exit on INT");
        exit (0);
    }
}

static FILE *open_test_outfile (const char *fn)
{
    FILE *fp;
    if (!fn) 
        fp = NULL; 
    else if ( !(fp = fopen (fn, "w"))) 
        fprintf (stderr, "Failed to open %s\n", fn);
    return fp;
}

static JSON parse_json_str (const char *jcbstr)
{
    int len = 0;
    JSON jcb = NULL;
    struct json_tokener *tok = NULL;

    if (jcbstr) 
        len = strnlen (jcbstr, sysconf (_SC_ARG_MAX));
    if (!(tok = json_tokener_new ())) 
        errno = ENOMEM;
    else if (!(jcb = json_tokener_parse_ex (tok, jcbstr, len)))
        errno = EPROTO;

    if (tok)
        json_tokener_free (tok);

    return jcb;
}

static inline void get_jobid (JSON jcb, int64_t *j)
{
    Jget_int64 (jcb, JSC_JOBID, j);
}

static inline void get_states (JSON jcb, int64_t *os, int64_t *ns)
{
    JSON o = NULL;
    Jget_obj (jcb, JSC_STATE_PAIR, &o);
    Jget_int64 (o, JSC_STATE_PAIR_OSTATE, os);
    Jget_int64 (o, JSC_STATE_PAIR_NSTATE, ns);
}


/******************************************************************************
 *                                                                            *
 *                      Async notification callback                           *
 *                                                                            * 
 ******************************************************************************/

static int job_status_cb (JSON jcb, void *arg, int errnum)
{
    int64_t os = 0;
    int64_t ns = 0;
    int64_t j = 0;
    jstatctx_t *ctx = NULL;
    flux_t h = (flux_t)arg;

    ctx = getctx (h);
    if (errnum > 0) {
        flux_log (ctx->h, LOG_ERR, "job_status_cb: errnum passed in");
        return -1;
    }

    get_jobid (jcb, &j);
    get_states (jcb, &os, &ns);
    Jput (jcb);

    fprintf (ctx->op, "%s->%s\n", 
    jsc_job_num2state ((job_state_t)os), 
    jsc_job_num2state ((job_state_t)ns));
    fflush (ctx->op);

    return 0;
}


/******************************************************************************
 *                                                                            *
 *      Top-level Handlers for each of notify, query and update commands      *
 *                                                                            *
 ******************************************************************************/

static int handle_notify_req (flux_t h, const char *ofn)
{
    jstatctx_t *ctx = NULL;

    sig_flux_h = h;
    if (signal (SIGINT, sig_handler) == SIG_ERR) 
        return -1;

    ctx = getctx (h);
    ctx->op = (ofn)?  open_test_outfile (ofn) : stdout;

    if (jsc_notify_status (h, job_status_cb, (void *)h) != 0) {
        flux_log (h, LOG_ERR, "failed to reg a job status change CB");
        return -1; 
    }
    if (flux_reactor_start (h) < 0) 
        flux_log (h, LOG_ERR, "error in flux_reactor_start");

    return 0;
}

static int handle_query_req (flux_t h, int64_t j, const char *k, const char *n)
{
    JSON jcb = NULL;
    jstatctx_t *ctx = NULL;

    ctx = getctx (h);
    ctx->op = n? open_test_outfile (n) : stdout;
    if (jsc_query_jcb (h, j, k, &jcb) != 0) {
        flux_log (h, LOG_ERR, "jsc_query_jcb reported an error\n");
        return -1;
    }
    fprintf (ctx->op, "Job Control Block: attribute %s for job %ld\n", k, j);
    fprintf (ctx->op, "%s\n", 
        json_object_to_json_string_ext (jcb, JSON_C_TO_STRING_PRETTY));    
    Jput (jcb);
    return 0;
}

static int handle_update_req (flux_t h, int64_t j, const char *k, 
                              const char *s, const char *n)
{
    JSON jcb = NULL;
    jstatctx_t *ctx = NULL;
    ctx = getctx (h);
    ctx->op = n?  open_test_outfile (n) : stdout;

    if (!(jcb = parse_json_str (s))) {
        flux_log (h, LOG_ERR, "parse_json_str parse error.\n");
        return -1;
    }
    return (jsc_update_jcb (ctx->h, j, k, jcb));
}


/******************************************************************************
 *                                                                            *
 *                            Main entry point                                *
 *                                                                            *
 ******************************************************************************/

int main (int argc, char *argv[])
{
    flux_t h; 
    int ch = 0;
    int rc = 0;
    char *cmd = NULL;
    const char *j = NULL;
    const char *ofn = NULL;
    const char *attr = NULL;
    const char *jcbstr = NULL;

    log_init ("flux-jstat");
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'o': /* --testout */
                ofn = xasprintf ("%s", optarg);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();

    if (!(h = flux_open  (NULL, 0)))
        err_exit ("flux_open");

    flux_log_set_facility (h, "jstat");
    cmd = argv[optind++];

    if (!strcmp ("notify", cmd))
        rc = handle_notify_req (h, (const char *)ofn);
    else if (!strcmp ("query", cmd)) {
        j = (const char *)(*(argv+optind));
        attr = (const char *)(*(argv+optind+1));
        rc = handle_query_req (h, strtol (j, NULL, 10), attr, ofn); 
    }
    else if (!strcmp ("update", cmd)) {
        j = (const char *)(*(argv+optind));
        attr = (const char *)(*(argv+optind+1));
        jcbstr = (const char *)(*(argv+optind+2));
        rc = handle_update_req (h, strtol (j, NULL, 10), attr, jcbstr, ofn);
    }
    else 
        usage ();

    flux_close (h);
    log_fini ();

    return (!rc)? 0: 42;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
