#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <json/json.h> /* XXX required by cmb.h for now */

#include "cmb.h"

int
main(int argc, char *argv[])
{
        int id, ntasks;
	struct timeval t1, t2, delta;
	cmb_t ctx;
	char kbuf[64], vbuf[64], *vp, *tmp;
	int errcount, putcount;

	if (!(ctx = cmb_init ())) {
		fprintf (stderr, "cmb_init: %s\n", strerror (errno));
		return 1;
	}
	if (!(tmp = getenv ("SLURM_NPROCS"))) {
		fprintf (stderr, "getenv SLURM_NPROCS failed");
		return 1;
	}
	ntasks = strtoul (tmp, NULL, 10);
	if (!(tmp = getenv ("SLURM_PROCID"))) {
		fprintf (stderr, "getenv SLURM_PROCID failed");
		return 1;
	}
	id = strtoul (tmp, NULL, 10);

	gettimeofday (&t1, NULL);

	/* one put & commit per rank */
	snprintf (kbuf, sizeof (kbuf), "kvstest.%d", id);
	snprintf (vbuf, sizeof (vbuf), "sandwich.%d", id);
	if (cmb_kvs_put (ctx, kbuf, vbuf) < 0) {
		fprintf (stderr, "%d: cmb_kvs_put: %s\n", id, strerror (errno));
		return 1;
	}
	if (cmb_kvs_commit (ctx, &errcount, &putcount) < 0) {
		fprintf (stderr, "%d: cmb_kvs_commit: %s\n",
			 id, strerror (errno));
		return 1;
	}
	if (errcount > 0 || putcount < 1) {
		fprintf (stderr, "%d: cmb_kvs_commit: errs=%d puts=%d\n",
			 id, errcount, putcount);
		return 1;
	}

	/* barrier */
	if (cmb_barrier (ctx, "kvstest", ntasks) < 0) {
		fprintf (stderr, "%d: cmb_barrier: %s\n",
			 id, strerror (errno));
		return 1;
	}
	gettimeofday (&t2, NULL);
	timersub (&t2, &t1, &delta);
	if (id == 0) {
		fprintf (stderr, "0: put phase took %lu.%.3lu sec\n",
			delta.tv_sec, delta.tv_usec / 1000);
	}

	gettimeofday (&t1, NULL);

	/* one get per rank */
	snprintf (kbuf, sizeof (kbuf), "kvstest.%d", 
		  id > 0 ? id - 1 : ntasks - 1);
	if (!(vp = cmb_kvs_get (ctx, kbuf))) {
		fprintf (stderr, "%d: cmb_kvs_get: %s\n", id, strerror (errno));
		return 1;
	}
	snprintf (vbuf, sizeof (vbuf), "sandwich.%d", 
		  id > 0 ? id - 1 : ntasks - 1);
	if (strcmp (vp, vbuf) != 0) {
		fprintf (stderr, "%d: cmb_kvs_get: expected %s got %s\n",
			 id, vbuf, vp);
		return 1;
	}
	free (vp);

	if (cmb_barrier (ctx, "kvstest2", ntasks) < 0) {
		fprintf (stderr, "%d: cmb_barrier: %s\n",
			 id, strerror (errno));
		return 1;
	}
	gettimeofday (&t2, NULL);
	timersub (&t2, &t1, &delta);
	if (id == 0) {
		fprintf (stderr, "0: get phase took %lu.%.3lu sec\n",
			delta.tv_sec, delta.tv_usec / 1000);
	}

	cmb_fini (ctx);

        return 0;
}
