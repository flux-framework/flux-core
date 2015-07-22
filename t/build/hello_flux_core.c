#include <flux/core.h>

int main (int argc, char **argv)
{
	flux_msg_t *msg = flux_msg_create (FLUX_MSGTYPE_EVENT);
	if (!msg)
		return 1;
	flux_msg_destroy (msg);

	return 0;
}
