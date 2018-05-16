#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "src/common/libtap/tap.h"
#include "src/common/libidset/idset.h"

struct inout {
    const char *in;
    int flags;
    const char *out;
};

struct inout test_inputs[] = {
    { "2",              0,          "2" },
    { "7-9",            0,          "7,8,9" },
    { "9-7",            0,          "7,8,9" },
    { "1,7-9",          0,          "1,7,8,9" },
    { "1,7-9,16",       0,          "1,7,8,9,16" },
    { "1,7-9,14,16",    0,          "1,7,8,9,14,16" },
    { "1-3,7-9,14,16",  0,          "1,2,3,7,8,9,14,16" },
    { "3,2,4,5",        0,          "2,3,4,5" },
    { "",               0,          ""},
    { "1048576",        0,          "1048576"},

    { "[2]",            0,          "2" },
    { "[7-9]",          0,          "7,8,9" },
    { "[9-7]",          0,          "7,8,9" },
    { "[3,2,4,5]",      0,          "2,3,4,5" },
    { "[]",             0,          ""},

    { "2",              IDSET_FLAG_RANGE,  "2" },
    { "7-9",            IDSET_FLAG_RANGE,  "7-9" },
    { "9-7",            IDSET_FLAG_RANGE,  "7-9" },
    { "1,7-9",          IDSET_FLAG_RANGE,  "1,7-9" },
    { "1,7-9,16",       IDSET_FLAG_RANGE,  "1,7-9,16" },
    { "1,7-9,14,16",    IDSET_FLAG_RANGE,  "1,7-9,14,16" },
    { "1-3,7-9,14,16",  IDSET_FLAG_RANGE,  "1-3,7-9,14,16" },
    { "3,2,4,5",        IDSET_FLAG_RANGE,  "2-5" },
    { "",               IDSET_FLAG_RANGE,  ""},

    { "2",             IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "2" },
    { "7-9",           IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[7-9]" },
    { "9-7",           IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[7-9]" },
    { "1,7-9",         IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9]" },
    { "1,7-9,16",      IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9,16]" },
    { "1,7-9,14,16",   IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1,7-9,14,16]" },
    { "1-3,7-9,14,16", IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[1-3,7-9,14,16]"},
    { "3,2,4,5",       IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, "[2-5]" },
    { "",              IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS, ""},

    { NULL, 0, NULL },
};

void test_basic (void)
{
    struct idset *idset;

    idset = idset_create (100, 0);
    ok (idset != NULL,
        "idset_create works");

    idset_destroy (idset);
}

void test_codec (void)
{
    struct inout *ip;

    for (ip = &test_inputs[0]; ip->in != NULL; ip++) {
        struct idset *idset;
        char *s;

        idset = idset_decode (ip->in);
        ok (idset != NULL,
            "idset_decode '%s' works", ip->in);
        s = idset_encode (idset, ip->flags);
        bool match = (s == NULL && ip->out == NULL)
                  || (s && ip->out && !strcmp (s, ip->out));
        ok (match == true,
            "idset_encode flags=0x%x '%s' works",
            ip->flags, ip->out ? ip->out : "NULL");
        if (!match)
            diag ("%s", s ? s : "NULL");
        free (s);
        idset_destroy (idset);
    }
}

/* Try a big one to cover encode buffer growth */
void test_codec_large (void)
{
    struct idset *idset;
    char *s;

    idset = idset_decode ("0-5000");
    ok (idset != NULL,
        "idset_decode '0-5000' works");
    s = idset_encode (idset, 0);
    int count = 0;
    if (s) {
        char *a1 = s;
        char *tok;
        char *saveptr;
        while ((tok = strtok_r (a1, ",", &saveptr))) {
            int i = strtol (tok, NULL, 10);
            if (i != count)
                break;
            count++;
            a1 = NULL;
        }
    }
    ok (count == 5001,
        "idset_encode flags=0x0 '0,2,3,...,5000' works");
    if (count != 5001)
        diag ("count=%d", count);
    free (s);
    idset_destroy (idset);
}

void test_badparam (void)
{
    struct idset *idset;

    if (!(idset = idset_create (100, 0)))
        BAIL_OUT ("idset_create failed");

    errno = 0;
    ok (idset_create (0, 0) == NULL && errno == EINVAL,
        "idset_create(slots=0) fails with EINVAL");
    errno = 0;
    ok (idset_create (1000, IDSET_FLAG_BRACKETS) == NULL && errno == EINVAL,
        "idset_create(flags=wrong) fails with EINVAL");


    errno = 0;
    ok (idset_encode (NULL, 0) == NULL && errno == EINVAL,
        "idset_encode(idset=NULL) fails with EINVAL");
    errno = 0;
    ok (idset_encode (idset, IDSET_FLAG_AUTOGROW) == NULL && errno == EINVAL,
        "idset_encode(flags=wrong) fails with EINVAL");
    errno = 0;
    ok (idset_decode (NULL) == NULL && errno == EINVAL,
        "idset_decode(s=NULL) fails with EINVAL");

    idset_destroy (idset);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_badparam ();
    test_codec ();
    test_codec_large ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
