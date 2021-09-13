#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <pmix_server.h>

#include "src/common/libtap/tap.h"

#include "plugins/pmix/codec.h"

void check_data (void)
{
    json_t *o;
    char in_buf[64];
    int in_len;
    void *out_buf;
    size_t out_len;

    snprintf (in_buf, sizeof (in_buf), "foobar");
    in_len = strlen (in_buf) + 1;
    out_buf = NULL;
    out_len = -1;
    ok ((o = pp_data_encode (in_buf, in_len)) != NULL,
        "pp_data_encode %d bytes works", in_len);
    ok (pp_data_decode (o, &out_buf, &out_len) == 0,
        "pp_data_decode works");
    diag ("out_len = %d", out_len);
    ok (out_len == in_len
        && out_buf != NULL
        && memcmp (in_buf, out_buf, out_len) == 0,
        "pp_data_decode returned the correct value");
    free (out_buf);
}

void check_pointer (void)
{
    json_t *o;
    void *ptr_in;
    void *ptr_out;

    ptr_in = (void *)~0LL;
    ptr_out = (void *)0xdeadbeef;
    ok ((o = pp_pointer_encode (ptr_in)) != NULL,
        "pp_pointer_encode ~0LL works");
    ok (pp_pointer_decode (o, &ptr_out) == 0,
        "pp_pointer_decode works");
    ok (ptr_in == ptr_out,
        "pp_pointer_decode returned the correct value");

    ptr_in = 0;
    ptr_out = (void *)0xdeadbeef;
    ok ((o = pp_pointer_encode (ptr_in)) != NULL,
        "pp_pointer_encode 0 works");
    ok (pp_pointer_decode (o, &ptr_out) == 0,
        "pp_pointer_decode works");
    ok (ptr_in == ptr_out,
        "pp_pointer_decode returned the correct value");
}

int main (int argc, char **argv)
{
    plan (NO_PLAN);

    check_pointer ();
    check_data ();

    done_testing ();
    return 0;
}

// vi:ts=4 sw=4 expandtab
