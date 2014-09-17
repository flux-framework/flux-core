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

/* tenc.c - encode/decode data on stdin/stdout as zmq encapsulated json */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/time.h>
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
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/zfd.h"

#define OPTIONS "dZJB"
static const struct option longopts[] = {
   {"decode",   no_argument,        0, 'd'},
   {"dump-zmq", no_argument,        0, 'Z'},
   {"dump-json", no_argument,       0, 'J'},
   {"dump-enc", no_argument,     0, 'B'},
   {0, 0, 0, 0},
};


json_object *buf_to_json (int seq, uint8_t *buf, int len)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_int (o, "seq", seq);
    util_json_object_add_int (o, "len", len);
    util_json_object_add_data (o, "dat", buf, len);
    return o;
}

void json_to_buf (json_object *o, int *seqp, uint8_t **bufp, int *lenp)
{
    int len;

    if (util_json_object_get_int (o, "seq", seqp) < 0
            || util_json_object_get_int (o, "len", &len) < 0
            || util_json_object_get_data (o, "dat", bufp, lenp) < 0
            || len != *lenp) {
        fprintf (stderr, "error decoding json\n");
        exit (1);
    }
}

const char *json_to_data (json_object *o)
{
    const char *s;

    if (util_json_object_get_string (o, "dat", &s) < 0)
        msg_exit ("error decoding json");
    return s;
}

json_object *zmsg_to_json (zmsg_t *zmsg)
{
    json_object *o;
    zframe_t *zf;
    char *s;

    zf = zmsg_first (zmsg);
    assert (zf != NULL);
    if (!(s = zframe_strdup (zf)))
        oom ();
    o = json_tokener_parse (s);
    free (s);
    return o;
}

zmsg_t *json_to_zmsg (json_object *o)
{
    const char *s;
    zmsg_t *zmsg;

    if (!(zmsg = zmsg_new ()))
        oom ();
    s = json_object_to_json_string (o);
    if (zmsg_addmem (zmsg, s, strlen (s)) < 0)
        oom ();
    return zmsg;
}

void usage (void)
{
    fprintf (stderr,
"Usage: tenc --encode\n"
"       tenc --decode [--dump-zmq|--dump-json|--dump-enc]\n"
);
    exit (1);
}

void write_all (int fd, uint8_t *buf, size_t len)
{
    int n, count = 0;

    do {
        n = write (fd, buf + count, len - count);
        if (n < 0) {
            perror ("write");
            exit (1);
        }
        count += n; 
    } while (count < len);
}

void encode (void)
{
    uint8_t buf[4096];
    json_object *o;
    zmsg_t *zmsg;
    int n, seq = 0;

    while ((n = read (STDIN_FILENO, buf, sizeof (buf))) > 0) {
        o = buf_to_json (seq, buf, n);
        assert (o != NULL);
        zmsg = json_to_zmsg (o);
        if (zfd_send (STDOUT_FILENO, &zmsg) < 0) {
            perror ("zfd_send");
            exit (1);
        }
        json_object_put (o);
        seq++;
    }
    if (n < 0) {
        perror ("stdin");
        exit (1);
    }
}

void decode (bool Zopt, bool Jopt, bool Bopt)
{
    zmsg_t *zmsg;
    const char *s;
    json_object *o;
    uint8_t *rbuf;
    int rlen, rseq;

    while ((zmsg = zfd_recv (STDIN_FILENO, false))) {
        if (Zopt) {
            zmsg_dump (zmsg);
        } else {
            o = zmsg_to_json (zmsg);
            if (Bopt) {
                s = json_to_data (o);
                printf ("%s\n", s);
            } else if (Jopt) {
                s = json_object_to_json_string (o);
                printf ("%s\n", s);
            } else {
                json_to_buf (o, &rseq, &rbuf, &rlen);
                if (rbuf) {
                    write_all (STDOUT_FILENO, rbuf, rlen);
                    free (rbuf);
                }
            }
            json_object_put (o);
        }
        zmsg_destroy (&zmsg);
    }
}

int main (int argc, char *argv[])
{
    int ch;
    bool dopt = false;
    bool Zopt = false;
    bool Jopt = false;
    bool Bopt = false;

    log_init (basename (argv[0]));

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'd':
                dopt = true;
                break;
            case 'Z':
                Zopt = true;
                break;
            case 'J':
                Jopt = true;
                break;
            case 'B':
                Bopt = true;
                break;
            default:
                usage ();
        }
    }

    if (dopt)
        decode (Zopt, Jopt, Bopt);
    else
        encode ();

    log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
