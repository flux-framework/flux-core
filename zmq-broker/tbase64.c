/* tbase64.c - encode/decode data on stdin/stdout as zmq encapsulated json */

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
#include "zmq.h"

#define OPTIONS "dZJ"
static const struct option longopts[] = {
   {"decode",   no_argument,        0, 'd'},
   {"dump-zmq", no_argument,        0, 'Z'},
   {"dump-json", no_argument,       0, 'J'},
   {0, 0, 0, 0},
};


json_object *buf_to_json (int seq, uint8_t *buf, int len)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_int (o, "seq", seq);
    util_json_object_add_int (o, "len", len);
    util_json_object_add_base64 (o, "dat", buf, len);
    return o;
}

void json_to_buf (json_object *o, int *seqp, uint8_t **bufp, int *lenp)
{
    int len;
    if (util_json_object_get_int (o, "seq", seqp) < 0
            || util_json_object_get_int (o, "len", &len) < 0
            || util_json_object_get_base64 (o, "dat", bufp, lenp) < 0
            || len != *lenp) {
        fprintf (stderr, "error decoding json\n");
        exit (1);
    }
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
    if (zmsg_addstr (zmsg, "%s", s) < 0)
    //if (zmsg_addmem (zmsg, s, strlen (s)) < 0)
        oom ();
    return zmsg;
}

void usage (void)
{
    fprintf (stderr,
"Usage: tbase64 --encode\n"
"       tbase64 --decode [--dump-zmq|--dump-json]\n"
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
        if (zmsg_send_fd (STDOUT_FILENO, &zmsg) < 0) {
            perror ("zmsg_send_fd");
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

void decode (bool Zopt, bool Jopt)
{
    zmsg_t *zmsg;
    const char *s;
    json_object *o;
    uint8_t *rbuf;
    int rlen, rseq;

    while ((zmsg = zmsg_recv_fd (STDIN_FILENO, false))) {
        if (Zopt) {
            zmsg_dump (zmsg);
        } else {
            o = zmsg_to_json (zmsg);
            if (Jopt) {
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
            default:
                usage ();
        }
    }

    if (dopt)
        decode (Zopt, Jopt);
    else
        encode ();

    log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
