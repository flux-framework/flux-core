/* cmbutil.c - exercise public interfaces */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>

#include "cmb.h"
#include "log.h"
#include "util.h"

#define OPTIONS "psb:k:SK:Ct:P:d:fF:n:l"
static const struct option longopts[] = {
    {"ping",       no_argument,        0, 'p'},
    {"ping-padding", required_argument,0, 'P'},
    {"ping-delay", required_argument,  0, 'd'},
    {"fdopen-read",no_argument,        0, 'f'},
    {"fdopen-write",required_argument, 0, 'F'},
    {"snoop",      no_argument,        0, 's'},
    {"barrier",    required_argument,  0, 'b'},
    {"nprocs",     required_argument,  0, 'n'},
    {"kvs-put",    required_argument,  0, 'k'},
    {"kvs-get",    required_argument,  0, 'K'},
    {"kvs-commit", no_argument,        0, 'C'},
    {"kvs-torture",required_argument,  0, 't'},
    {"sync",       no_argument,        0, 'S'},
    {"live-query", no_argument,        0, 'l'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: cmbutil OPTIONS\n"
"  -p,--ping              loop back a sequenced message through the cmb\n"
"  -P,--ping-padding N    pad ping packets with N bytes (adds a JSON string)\n"
"  -d,--ping-delay N      set delay between ping packets (in msec)\n"
"  -f,--fdopen-read       open r/o fd, print name, and read until EOF\n"
"  -F,--fdopen-write DEST open w/o fd, routing data to DEST, write until EOF\n"
"  -b,--barrier name      execute barrier across slurm job\n"
"  -n,--nprocs N          override nprocs (default $SLURM_NPROCS or 1)\n"
"  -k,--kvs-put key=val   set a key\n"
"  -K,--kvs-get key       get a key\n"
"  -C,--kvs-commit        commit pending kvs puts\n"
"  -t,--kvs-torture N     set N keys, then commit\n"
"  -s,--snoop substr      watch bus traffic matching subscription\n"
"  -S,--sync              block until event.sched.triger\n"
"  -l,--live-query        get list of up nodes\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    cmb_t c;
    int nprocs;
    int padding = 0;
    int pingdelay_ms = 1000;

    log_init (basename (argv[0]));

    nprocs = env_getint ("SLURM_NPROCS", 1);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'P': /* --ping-padding N */
                padding = strtoul (optarg, NULL, 10);
                break;
            case 'd': /* --ping-delay N */
                pingdelay_ms = strtoul (optarg, NULL, 10);
                break;
            case 'n': /* --nprocs N */
                nprocs = strtoul (optarg, NULL, 10);
                break;
        }
    }
    if (!(c = cmb_init ()))
        err_exit ("cmb_init");
    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'p': { /* --ping */
                int i;
                struct timeval t, t1, t2;
                for (i = 0; ; i++) {
                    xgettimeofday (&t1, NULL);
                    if (cmb_ping (c, i, padding) < 0)
                        err_exit ("cmb_ping");
                    xgettimeofday (&t2, NULL);
                    timersub (&t2, &t1, &t);
                    msg ("loopback ping pad=%d seq=%d time=%0.3f ms", padding,i,
                      (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);
                    usleep (pingdelay_ms * 1000);
                }
                break;
            }
            case 'f': { /* --fdopen-read */
                int fd, n;
                char *name;
                char buf[CMB_API_FD_BUFSIZE];

                if ((fd = cmb_fd_open (c, NULL, &name)) < 0)
                    err_exit ("cmb_fd_open");
                msg ("fdopen-read: %s", name);
                while ((n = read (fd, buf, sizeof (buf))) > 0) {
                    int done, m;
                    for (done = 0; done < n; done += m) {
                        m = write (1, buf + done, n - done);
                        if (m < 0)
                            err_exit ("write stdout");
                    }
                }
                if (n < 0)
                    err_exit ("fdopen-read");
                if (n == 0)
                    msg ("fdopen-read: EOF");
                if (close (fd) < 0)
                    err_exit ("close");
                break;
            }
            case 'F': { /* --fdopen-write DEST */
                int fd, n;
                char buf[CMB_API_FD_BUFSIZE];

                if ((fd = cmb_fd_open (c, optarg, NULL)) < 0)
                    err_exit ("cmb_fd_open");
                while ((n = read (0, buf, sizeof (buf))) > 0) {
                    int m;
                    if ((m = write (fd, buf, n)) < 0)
                        err_exit ("fdopen-write");
                    if (m < n)
                        msg_exit ("fdopen-write: short write");
                }
                if (n < 0)
                    err_exit ("read stdin");
                if (close (fd) < 0)
                    err ("close");
                break;
            }
            case 'b': { /* --barrier NAME */
                struct timeval t, t1, t2;
                xgettimeofday (&t1, NULL);
                if (cmb_barrier (c, optarg, nprocs) < 0)
                    err_exit ("cmb_barrier");
                xgettimeofday (&t2, NULL);
                timersub (&t2, &t1, &t);
                msg ("barrier time=%0.3f ms",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);
                break;
            }
            case 's': { /* --snoop */
                if (cmb_snoop (c, "") < 0)
                    err_exit ("cmb_snoop");
                break;
            }
            case 'S': { /* --sync */
                if (cmb_sync (c) < 0)
                    err_exit ("cmb_sync");
                break;
            }
            case 'l': { /* --live-query */
                int i, *up = NULL, up_len, *dn = NULL, dn_len, nnodes;
                if (cmb_live_query (c, &up, &up_len, &dn, &dn_len, &nnodes) < 0)
                    err_exit ("cmb_live_query");
                printf ("up:   ");
                for (i = 0; i < up_len; i++)
                    printf ("%d%s", up[i], i == up_len - 1 ? "" : ",");
                printf ("\ndown: ");
                for (i = 0; i < dn_len; i++)
                    printf ("%d%s", dn[i], i == dn_len - 1 ? "" : ",");
                printf ("\n");
                if (up)
                    free (up); 
                if (dn)
                    free (dn); 
                break;
            }
            case 'k': { /* --kvs-put key=val */
                char *key = optarg;
                char *val = strchr (optarg, '=');
                if (val == NULL)
                    usage ();
                *val++ = '\0';
                if (cmb_kvs_put (c, key, val) < 0)
                    err_exit ("cmb_kvs_put");
                break;
            }
            case 'K': { /* --kvs-get key */
                char *val = cmb_kvs_get (c, optarg);
                if (!val && errno != 0)
                    err_exit ("cmb_kvs_get");
                printf ("%s=%s\n", optarg, val ? val : "<nil>");
                if (val)
                    free (val);
                break;
            }
            case 'C': { /* --kvs-commit */
                int errcount, putcount;

                if (cmb_kvs_commit (c, &errcount, &putcount) < 0)
                    err_exit ("cmb_kvs_commit");
                printf ("errcount=%d putcount=%d\n", errcount, putcount);
                break;
            }
            case 't': { /* --kvs-torture N */
                int i, n = strtoul (optarg, NULL, 10);
                char key[16], val[16], *rval;
                struct timeval t1, t2, t;
                int errcount, putcount;

                xgettimeofday (&t1, NULL);
                for (i = 0; i < n; i++) {
                    snprintf (key, sizeof (key), "key%d", i);
                    snprintf (val, sizeof (key), "val%d", i);
                    if (cmb_kvs_put (c, key, val) < 0)
                        err_exit ("cmb_kvs_put");
                }
                xgettimeofday (&t2, NULL);
                timersub(&t2, &t1, &t);
                msg ("kvs_put:    time=%0.3f ms",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);

                xgettimeofday (&t1, NULL);
                if (cmb_kvs_commit (c, &errcount, &putcount) < 0)
                    err_exit ("cmb_kvs_commit");
                xgettimeofday (&t2, NULL);
                timersub (&t2, &t1, &t);
                msg ("kvs_commit: time=%0.3f ms errcount=%d putcount=%d",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000,
                     errcount, putcount);

                xgettimeofday (&t1, NULL);
                for (i = 0; i < n; i++) {
                    snprintf (key, sizeof (key), "key%d", i);
                    snprintf (val, sizeof (key), "val%d", i);
                    if (!(rval = cmb_kvs_get (c, key)))
                        err_exit ("cmb_kvs_get");
                    if (strcmp (rval, val) != 0)
                        msg_exit ("cmb_kvs_get: incorrect val");
                    free (rval);
                }
                xgettimeofday (&t2, NULL);
                timersub(&t2, &t1, &t);
                msg ("kvs_get:    time=%0.3f ms",
                     (double)t.tv_sec * 1000 + (double)t.tv_usec / 1000);

                break;
            }
            case 'P':
            case 'd':
            case 'n':
                break; /* handled in first getopt */
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();

    cmb_fini (c);
    exit (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
