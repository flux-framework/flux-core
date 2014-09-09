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
#include <stdio.h>
#include <czmq.h>
#include <json.h>

#include "zio.h"

int output_thread_cb (zloop_t *zl, zmq_pollitem_t *zp, zio_t z)
{
	zmsg_t *zmsg;
	char *name;
	char *buf;

	zmsg = zmsg_recv (zp->socket);
	if (!zmsg) {
		return (-1);
	}
	name = zmsg_popstr (zmsg);
	buf = zmsg_popstr (zmsg);
	json_object *o = json_tokener_parse (buf);
	zio_write_json (z, o);
	zmsg_destroy (&zmsg);
	free (name);
	free (buf);
	json_object_put (o);
	if (zio_closed (z))
		return (-1);  /* Wakeup zloop if we're done */
	return (0);
}

int close_cb (zio_t zio, void *pipe)
{
	fprintf (stderr, "thread zio object closed\n");
	return (-1); /* Wake up zloop */
}

int close_cb_main (zio_t zio, void *pipe)
{
	fprintf (stderr, "main zio object closed\n");
	return (-1); /* Wake up zloop */
}

void othr (void *args, zctx_t *zctx, void *pipe)
{
	zloop_t *zl = zloop_new();
	zio_t out = zio_writer_create ("stdout", STDOUT_FILENO, pipe);
	zmq_pollitem_t zp = {   .fd = -1,
				.socket = pipe,
				.events = ZMQ_POLLIN|ZMQ_POLLERR };

	zloop_poller (zl, &zp, (zloop_fn *) output_thread_cb, (void *) out);
	zio_set_close_cb (out, &close_cb);
	zio_set_debug (out, "thread out", NULL);

	zio_zloop_attach (out, zl);
	zloop_start (zl);
	fprintf (stderr, "Done with thread, signaling parent...\n");
	zstr_send (pipe, "");
	return;
}


int main (int ac, char ** av)
{
	char *s;
	void *zs;
	zio_t in;
	zctx_t *zctx = zctx_new ();
	zloop_t *zloop = zloop_new ();

	if ((zs = zthread_fork (zctx, othr, NULL)) == NULL) {
		fprintf (stderr, "zthread_fork failed\n");
		exit (1);
	}

	in = zio_reader_create ("stdout", dup(STDIN_FILENO), zs, NULL);
	zio_zloop_attach (in, zloop);
	zio_set_close_cb (in, &close_cb_main);
	zio_set_debug (in, "main thread in", NULL);

	printf ("starting zloop in parent\n");
	zloop_set_verbose (zloop, 1);
	zloop_start (zloop);
	fprintf (stderr, "zloop complete\n");
	s = zstr_recv (zs);
	fprintf (stderr, "child thread complete\n");
	zmq_close (zs);
	zmq_term (zctx);
	return (0);
}
