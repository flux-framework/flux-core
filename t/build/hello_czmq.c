#include <czmq.h>

int main (int argc, char **argv)
{
	zlist_t *zlist = zlist_new ();
	zlist_destroy (&zlist);

	return 0;
}
