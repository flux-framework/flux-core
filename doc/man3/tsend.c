#include <flux/core.h>
#include "src/common/libutil/log.h"

int main (int argc, char **argv)
{
    flux_t *h;
    flux_msg_t *msg;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(msg = flux_event_encode ("snack.bar.closing", NULL)))
        log_err_exit ("flux_event_encode");
    if (flux_send (h, msg, 0) < 0)
        log_err_exit ("flux_send");
    flux_msg_destroy (msg);
    flux_close (h);
    return (0);
}
