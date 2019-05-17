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
#    include "config.h"
#endif
#include <stdio.h>
#include <libgen.h>
#include <argz.h>
#include <ctype.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"

static int event_pub (optparse_t *p, int argc, char **argv);
static int event_sub (optparse_t *p, int argc, char **argv);

static void event_pub_register (optparse_t *p);
static void event_sub_register (optparse_t *p);

int main (int argc, char *argv[])
{
    int n;
    optparse_t *p;
    flux_t *h;

    log_init ("flux-event");
    if (!(p = optparse_create ("flux-event")))
        log_err_exit ("optparse_create");

    event_pub_register (p);
    event_sub_register (p);

    n = optparse_parse_args (p, argc, argv);
    if (n == argc || n <= 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    optparse_set_data (p, "handle", h);
    if (optparse_run_subcommand (p, argc, argv) < 0)
        log_err_exit ("subcommand");

    flux_close (h);
    optparse_destroy (p);
    log_fini ();
    return 0;
}

static bool match_payload (const char *s1, const char *s2)
{
    if (!s1 && !s2)
        return true;
    if (!s2 || !s1)
        return false;
    return !strcmp (s1, s2);
}

static bool match_payload_raw (const void *p1,
                               int p1sz,
                               const char *p2,
                               int p2sz)
{
    if (!p1 && !p2)
        return true;
    if (p1sz != p2sz)
        return false;
    return !memcmp (p1, p2, p1sz);
}

static int publish_raw_sync (flux_t *h,
                             const char *topic,
                             int flags,
                             char *payload,
                             int payloadsz)
{
    flux_future_t *f;
    int seq;
    int rc = -1;
    if (!(f = flux_event_publish_raw (h, topic, flags, payload, payloadsz)))
        goto done;
    if (flux_event_publish_get_seq (f, &seq) < 0)
        goto done;
    printf ("seq=%d\n", seq);
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int publish_raw (flux_t *h,
                        const char *topic,
                        int flags,
                        char *payload,
                        int payloadsz)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_event_encode_raw (topic, payload, payloadsz)))
        goto done;
    if ((flags & FLUX_MSGFLAG_PRIVATE) && flux_msg_set_private (msg) < 0)
        goto done;
    if (flux_send (h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int publish_json_sync (flux_t *h,
                              const char *topic,
                              int flags,
                              char *payload)
{
    flux_future_t *f;
    int seq;
    int rc = -1;

    if (!(f = flux_event_publish (h, topic, 0, payload)))
        goto done;
    if (flux_event_publish_get_seq (f, &seq) < 0)
        goto done;
    printf ("seq=%d\n", seq);
done:
    flux_future_destroy (f);
    rc = 0;
    return rc;
}

static int publish_json (flux_t *h, const char *topic, int flags, char *payload)
{
    flux_msg_t *msg;
    int rc = -1;

    if (!(msg = flux_event_encode (topic, payload)))
        goto done;
    if ((flags & FLUX_MSGFLAG_PRIVATE) && flux_msg_set_private (msg) < 0)
        goto done;
    if (flux_send (h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static struct optparse_option pub_opts[] =
    {{
         .name = "raw",
         .key = 'r',
         .has_arg = 0,
         .usage = "Interpret event payload as "
                  "raw.",
     },
     {
         .name = "synchronous",
         .key = 's',
         .has_arg = 0,
         .usage = "Wait for event sequence "
                  "assignment before exiting.",
     },
     {
         .name = "loopback",
         .key = 'l',
         .has_arg = 0,
         .usage = "Wait for published event to "
                  "be received before exiting.",
     },
     {
         .name = "private",
         .key = 'p',
         .has_arg = 0,
         .usage = "Set privacy flag on "
                  "published event.",
     },
     OPTPARSE_TABLE_END};

static void event_pub_register (optparse_t *parent)
{
    if (optparse_reg_subcommand (parent,
                                 "pub",
                                 event_pub,
                                 "[OPTIONS] topic [payload]",
                                 NULL,
                                 0,
                                 pub_opts)
        != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_reg_subcommand");
}

static int event_pub (optparse_t *p, int argc, char **argv)
{
    flux_t *h = optparse_get_data (p, "handle");
    int optindex = optparse_option_index (p);
    char *topic;
    char *payload = NULL;
    int payloadsz = 0;
    int flags = 0;
    int rc;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    topic = argv[optindex++];

    /* Concatenate any remaining arguments to form payload.
     */
    if (optindex < argc) {
        size_t len = 0;
        int e;
        if ((e = argz_create (argv + optindex, &payload, &len)) != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (payload, len, ' ');
        if (optparse_hasopt (p, "raw"))
            payloadsz = strlen (payload);
    }

    if (optparse_hasopt (p, "private"))
        flags |= FLUX_MSGFLAG_PRIVATE;

    if (optparse_hasopt (p, "loopback")) {
        if (flux_event_subscribe (h, topic) < 0)
            log_err_exit ("flux_event_subscribe");
    }

    if (optparse_hasopt (p, "raw")) {
        if (optparse_hasopt (p, "synchronous"))
            rc = publish_raw_sync (h, topic, flags, payload, payloadsz);
        else
            rc = publish_raw (h, topic, flags, payload, payloadsz);
    } else {
        if (optparse_hasopt (p, "synchronous"))
            rc = publish_json_sync (h, topic, flags, payload);
        else
            rc = publish_json (h, topic, flags, payload);
    }
    if (rc < 0)
        log_err_exit ("publish failed");

    if (optparse_hasopt (p, "loopback")) {
        flux_msg_t *msg;
        struct flux_match match = FLUX_MATCH_EVENT;
        bool received = false;
        match.topic_glob = topic;

        while (!received) {
            if (!(msg = flux_recv (h, match, 0)))
                log_err_exit ("flux_recv error");
            if (optparse_hasopt (p, "raw")) {
                const void *data;
                int len;
                if ((flux_event_decode_raw (msg, NULL, &data, &len) == 0
                     && match_payload_raw (payload, payloadsz, data, len)))
                    received = true;
            } else {
                const char *json_str;
                if ((flux_event_decode (msg, NULL, &json_str) == 0
                     && match_payload (payload, json_str)))
                    received = true;
            }
            flux_msg_destroy (msg);
        }
    }

    free (payload);
    return (0);
}

static char *make_printable (const char *buf, int bufsz, int maxlen)
{
    int i;
    char *s;
    int len = bufsz < maxlen ? bufsz : maxlen;

    if (!(s = calloc (1, len + 1)))
        log_err_exit ("calloc");
    if (buf == NULL)
        return s;
    for (i = 0; i < len; i++) {
        if (bufsz > maxlen && i >= len - 3)  // indicate truncation w/ "..."
            s[i] = '.';
        else if (isprint (buf[i]))
            s[i] = buf[i];
        else
            s[i] = '.';
    }
    s[i] = '\0';
    return s;
}

static int subscribe_multiple (flux_t *h, int tc, char **tv)
{
    int i;

    if (tc == 0)
        return flux_event_subscribe (h, "");
    for (i = 0; i < tc; i++) {
        if (flux_event_subscribe (h, tv[i]) < 0)
            return -1;
    }
    return 0;
}

static int unsubscribe_multiple (flux_t *h, int tc, char **tv)
{
    int i;
    if (tc == 0)
        return flux_event_unsubscribe (h, "");
    for (i = 0; i < tc; i++) {
        if (flux_event_unsubscribe (h, tv[i]) < 0)
            return -1;
    }
    return 0;
}

static const struct optparse_option sub_opts[] = {{.name = "count",
                                                   .key = 'c',
                                                   .has_arg = 1,
                                                   .arginfo = "N",
                                                   .group = 1,
                                                   .usage =
                                                       "Process N events then "
                                                       "exit"},
                                                  OPTPARSE_TABLE_END};

void event_sub_register (optparse_t *parent)
{
    if (optparse_reg_subcommand (parent,
                                 "sub",
                                 event_sub,
                                 "[OPTIONS] [topic...]",
                                 NULL,
                                 0,
                                 sub_opts)
        != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_reg_subcommand");
}

static void event_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    optparse_t *p = arg;
    int max_count = optparse_get_int (p, "count", 0);
    static int recv_count = 0;
    const char *payload;
    int payloadsz;
    const char *topic;

    if (flux_event_decode (msg, &topic, &payload) == 0)
        printf ("%s\t%s\n", topic, payload ? payload : "");
    else if (flux_event_decode_raw (msg,
                                    &topic,
                                    (const void **)&payload,
                                    &payloadsz)
             == 0) {
        int maxlen = payloadsz;  // no truncation
        char *s = make_printable (payload, payloadsz, maxlen);
        printf ("%s\t%s\n", topic, s);
        free (s);
    } else
        printf ("malformed message ignored\n");
    if (max_count > 0 && ++recv_count == max_count)
        flux_msg_handler_stop (mh);
}

static int event_sub (optparse_t *p, int argc, char **argv)
{
    flux_t *h = optparse_get_data (p, "handle");
    int optindex = optparse_option_index (p);
    flux_reactor_t *r;
    flux_msg_handler_t *mh;

    if (!h)
        log_err_exit ("failed to get handle");
    if (!(r = flux_get_reactor (h)))
        log_err_exit ("failed to get reactor");

    /* Since output is line-based with undeterministic amount of time
     * between lines, force stdout to be line buffered so our output
     * is immediately available in stream, even if stdout is not a tty.
     */
    setlinebuf (stdout);

    if (subscribe_multiple (h, argc - optindex, argv + optindex) < 0)
        log_err_exit ("flux_event_subscribe");

    if (!(mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, event_cb, p)))
        log_err_exit ("flux_msg_handler_create");
    flux_msg_handler_start (mh);
    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");
    flux_msg_handler_destroy (mh);

    if (unsubscribe_multiple (h, argc - optindex, argv + optindex), 0)
        log_err_exit ("flux_event_subscribe");
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
