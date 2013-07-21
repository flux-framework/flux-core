#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <json/json.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <zmq.h>
#include <czmq.h>


#include "cmb.h"
#include "log.h"
#include "util.h"
#include "zmq.h"

static struct timespec diff (struct timespec start, struct timespec end)
{
	struct timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}

static double time_since (struct timespec t0)
{
	struct timespec ts, d;
	clock_gettime (CLOCK_MONOTONIC, &ts);

	d = diff (t0, ts);

	return ((double) d.tv_sec * 1000 + (double) d.tv_nsec / 1000000);
}

int util_json_object_get_int (json_object *o, char *name, int *ip)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *ip = json_object_get_int (no);
    return 0;
}

int util_json_object_get_string (json_object *o, char *name, const char **sp)
{
    json_object *no = json_object_object_get (o, name);
    if (!no)
        return -1;
    *sp = json_object_get_string (no);
    return 0;
}

void util_json_object_add_int (json_object *o, char *name, int i)
{
    json_object *no;

    if (!(no = json_object_new_int (i)))
	exit (1);
    json_object_object_add (o, name, no);
}
void util_json_object_add_string (json_object *o, char *name, const char *s)
{
    json_object *no;

    if (!(no = json_object_new_string (s)))
        exit (1);
    json_object_object_add (o, name, no);
}


int main (int ac, char **av)
{
	int i, id, ncopies = 1;
	char *tag;
	const char *s;
	struct timespec ts0;
	cmb_t c = cmb_init ();

	if (!c) {
		fprintf (stderr, "Failed to open connection to cmb!\n");
		exit (1);
	}
	json_object *o = json_object_new_object ();

	if (ac >= 3)
		ncopies = atoi(av[2]);

	util_json_object_add_int (o, "repeat", ncopies);
	util_json_object_add_string (o, "string", av[1]);

	clock_gettime (CLOCK_MONOTONIC, &ts0);
	if (cmb_send_message (c, o, "echo") < 0) {
		fprintf (stderr, "cmb_send_message failed!\n");
		exit (1);
	}


	for (i = 0; i < ncopies; i++) {
		zmsg_t *zmsg;
		double ms;

		if (!(zmsg = cmb_recv_zmsg (c, false))) {
			fprintf (stderr, "Failed to recv zmsg!\n");
			exit (1);
		}
		ms = time_since (ts0);
		//zmsg_dump_compact (zmsg);

		if (cmb_msg_decode (zmsg, &tag, &o) < 0) {
			fprintf (stderr, "cmb_msg_decode failed!\n");
			exit (1);
		}


		if (util_json_object_get_string (o, "string", &s) < 0) {
			fprintf (stderr, "get string failed!\n");
			fprintf (stderr, "Got:\n%s\n",
					json_object_to_json_string (o));
			exit (1);
		}

		util_json_object_get_int (o, "id", &id);
		fprintf (stderr, "%0.3fms: got reply %d from %d: %s\n",
			ms, i+1, id, s);
		zmsg_destroy (&zmsg);
	}

	exit(0);
}

