#include <flux/core.h>
#include "die.h"

int main (int argc, char **argv)
{
    flux_t *h;
    flux_msg_t *msg;

    if (!(h = flux_open (NULL, 0)))
        die ("flux open %s", "NULL");
    if (!(msg = flux_event_encode ("snack.bar.closing", NULL)))
        die ("flux_event_encode");
    if (flux_send (h, msg, 0) < 0)
        die ("flux_send");
    flux_msg_destroy (msg);
    flux_close (h);
    return (0);
}
