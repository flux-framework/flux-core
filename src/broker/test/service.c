#include <flux/core.h>
#include <czmq.h>
#include <stdio.h>

#include <flux/core.h>

#include "service.h"

#include "src/common/libtap/tap.h"

const flux_msg_t *foo_cb_msg;
void *foo_cb_arg;
int foo_cb_called;
int foo_cb_rc;
int foo_cb_errno;

static int foo_cb (const flux_msg_t *msg, void *arg)
{
    foo_cb_msg = msg;
    foo_cb_arg = arg;
    foo_cb_called++;

    if (foo_cb_rc != 0)
        errno = foo_cb_errno;

    return foo_cb_rc;
}


int main (int argc, char **argv)
{
    struct service_switch *sw;
    flux_msg_t *msg, *msg2, *msg3;

    plan (NO_PLAN);

    sw = service_switch_create ();
    ok (sw != NULL,
        "service_switch_create works");

    msg = flux_request_encode ("foo", NULL);
    if (!msg)
        BAIL_OUT ("flux_request_encode: %s", flux_strerror (errno));
    errno = 0;
    ok (service_send (sw, msg) < 0 && errno == ENOSYS,
        "service_send to 'foo' fails with ENOSYS");

    ok (service_add (sw, "foo", NULL, foo_cb, NULL) == 0,
        "service_add foo works");

    foo_cb_msg = NULL;
    foo_cb_arg = (void *)(uintptr_t)1;
    foo_cb_called = 0;
    foo_cb_rc = 0;
    ok (service_send (sw, msg) == 0,
        "service_send to 'foo' works");
    ok (foo_cb_called == 1 && foo_cb_arg == NULL && foo_cb_msg == msg,
        "and callback was called with expected arguments");

    foo_cb_rc = 42;
    foo_cb_errno = ENXIO;
    errno = 0;
    ok (service_send (sw, msg) == 42 && errno == ENXIO,
        "service_send returns callback's return code and preserves errno");

    service_remove (sw, "foo");
    errno = 0;
    ok (service_send (sw, msg) < 0 && errno == ENOSYS,
        "service_remove works");

    flux_msg_destroy (msg);


    msg = flux_request_encode ("bar.baz", NULL);
    if (!msg)
        BAIL_OUT ("flux_request_encode: %s", flux_strerror (errno));
    ok (service_add (sw, "bar", NULL, foo_cb, NULL) == 0,
        "service_add bar works");
    foo_cb_rc = 0;
    ok (service_send (sw, msg) == 0,
        "service_send to 'bar.baz' works");
    flux_msg_destroy (msg);

 #define SVC_NAME "reallylongservicenamewowthisisimpressive"
 #define SVC_ALT1 "alt1"
 #define SVC_ALT2 "alt2"
    ok (service_add (sw, SVC_NAME, "fakeuuid", foo_cb, NULL) == 0,
        "service_add works for long service name");
    ok (service_add (sw, SVC_ALT1, "fakeuuid", foo_cb, NULL) == 0,
        "service_add works for alternate service name 1");
    ok (service_add (sw, SVC_ALT2, "fakeuuid", foo_cb, NULL) == 0,
        "service_add works for alternate service name 2");

    msg = flux_request_encode (SVC_NAME ".baz", NULL);
    if (!msg)
        BAIL_OUT ("flux_request_encode: %s", flux_strerror (errno));
    msg2 = flux_request_encode (SVC_ALT1 ".oooh", NULL);
    if (!msg2)
        BAIL_OUT ("flux_request_encode: %s", flux_strerror (errno));
    msg3 = flux_request_encode (SVC_ALT2 ".vroom", NULL);
    if (!msg3)
        BAIL_OUT ("flux_request_encode: %s", flux_strerror (errno));

    foo_cb_rc = 0;
    foo_cb_called = 0;
    ok (service_send (sw, msg) == 0 && foo_cb_called == 1,
        "service_send matched long service name");
    ok (service_send (sw, msg2) == 0 && foo_cb_called == 2,
        "service_send matched first alternate name");
    ok (service_send (sw, msg3) == 0 && foo_cb_called == 3,
        "service_send matched second alternate name");

    service_remove_byuuid (sw, "fakeuuid");

    foo_cb_rc = 0;
    foo_cb_called = 0;
    errno = 0;
    ok (service_send (sw, msg) < 0 && errno == ENOSYS && foo_cb_called == 0,
        "service_send to long service name fails after remove_byuuid");
    errno = 0;
    ok (service_send (sw, msg2) < 0 && errno == ENOSYS && foo_cb_called == 0,
        "service_send to first alternate name fails after remove_byuuid");
    errno = 0;
    ok (service_send (sw, msg3) < 0  && errno == ENOSYS && foo_cb_called == 0,
        "service_send to second alternate name fails after remove_byuuid");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);
    flux_msg_destroy (msg3);

    service_switch_destroy (sw);

    done_testing ();

    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
