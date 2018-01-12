#include "src/modules/kvs/msg_cb_handler.h"
#include "src/common/libflux/message.h"
#include "src/common/libtap/tap.h"

void msghand (flux_t *h, flux_msg_handler_t *mh,
              const flux_msg_t *msg, void *arg)
{
    int *count = arg;
    (*count)++;
}
int main (int argc, char *argv[])
{
    msg_cb_handler_t *mcb;
    flux_msg_t *msg;
    const flux_msg_t *cpy;
    const char *str;
    int count;

    plan (NO_PLAN);

    /* corner cases */

    msg_cb_handler_destroy (NULL);

    ok (!msg_cb_handler_get_msgcopy (NULL),
        "msg_cb_handler_get_msgcopy returns NULL on bad input");

    /* test empty callback handler */

    ok ((mcb = msg_cb_handler_create (NULL, NULL, NULL, NULL, NULL)) != NULL,
        "msg_cb_handler_create works with all NULL inputs");

    msg_cb_handler_call (mcb);

    ok (!msg_cb_handler_get_msgcopy (mcb),
        "msg_cb_handler_get_msgcopy returns NULL for message copy on empty handler");

    msg_cb_handler_destroy (mcb);

    /* test filled callback handler */

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");

    ok (!flux_msg_pack (msg, "{ s:s }", "foo", "bar"),
        "flux_msg_pack works");

    /* msg handler cb & msg cannot be NULL */
    ok ((mcb = msg_cb_handler_create (NULL, NULL, msg, &count, msghand)) != NULL,
        "msg_cb_handler_create works");

    count = 0;
    msg_cb_handler_call (mcb);
    ok (count == 1,
        "msg_cb_handler_call calls callback correctly");

    msg_cb_handler_set_cb (mcb, NULL);

    count = 0;
    msg_cb_handler_call (mcb);
    ok (count == 0,
        "msg_cb_handler_call doesn't call callback after it was changed");

    ok ((cpy = msg_cb_handler_get_msgcopy (mcb)) != NULL,
        "msg_cb_handler_get_msgcopy returns message copy");

    ok (!flux_msg_unpack (cpy, "{ s:s }", "foo", &str)
        && !strcmp (str, "bar"),
        "msg_cb_handler_get_msgcopy returned correct msg copy");

    msg_cb_handler_destroy (mcb);

    flux_msg_destroy (msg);
    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

