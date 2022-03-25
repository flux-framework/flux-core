/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>

#include "src/common/libtap/tap.h"
#include "match.h"

struct match_test {
    const char *desc;
    const char *json;
    bool result;
};

struct validate_test {
    const char *desc;
    const char *json;
    int rc;
    const char *err;
};

/*  These tests all assume a rnode object foo0 with properties xx and yy.
 */
struct match_test match_tests[] = {
    { "empty json object matches everything", "{}", true },
    { "hostname property matches",
       "{\"properties\": [\"foo0\"]}",
       true
    },
    {  "empty properties dict matches everything",
       "{\"properties\": [] }",
       true
    },
    {  "property matches",
       "{\"properties\": [\"xx\"]}",
        true,
    },
    {  "logical not on property",
       "{\"properties\": [\"^xx\"]}",
        false,
    },
    {  "logical not on unset property",
       "{\"properties\": [\"^zz\"]}",
        true,
    },
    {  "property list matches like 'and'",
       "{\"properties\": [\"xx\", \"yy\"]}",
        true,
    },
    {  "property list match fails unless node has all",
       "{\"properties\": [\"xx\", \"zz\"]}",
        false,
    },
    { "property list match fails if property missing",
       "{\"properties\": [\"zz\"]}",
        false,
    },
    { "and with two true statements",
      "{\"and\": [ {\"properties\": [\"xx\"]}, \
                   {\"properties\": [\"yy\"]}  \
                 ]}",
        true,
    },
    { "and with one false statement",
      "{\"and\": [ {\"properties\": [\"xx\"]}, \
                   {\"properties\": [\"zz\"]}  \
                 ]}",
        false,
    },
    { "or with two true statements",
      "{\"or\": [ {\"properties\": [\"xx\"]}, \
                  {\"properties\": [\"yy\"]}  \
                 ]}",
        true,
    },
    { "or with one true statements",
      "{\"or\": [ {\"properties\": [\"zz\"]}, \
                  {\"properties\": [\"yy\"]}  \
                 ]}",
        true,
    },
    { "or with two false statements",
      "{\"or\": [ {\"properties\": [\"zz\"]}, \
                  {\"properties\": [\"aa\"]}  \
                 ]}",
        false,
    },
    { "not with or with one true statement",
      "{\"not\": [ \
        {\"or\": [ {\"properties\": [\"zz\"]}, \
                   {\"properties\": [\"yy\"]}  \
                 ]} \
        ] \
       }",
       false,
    },
    { NULL, NULL, false },
};

struct validate_test validate_tests[] = {
    { "non-object fails",
      "[]",
      -1,
      "constraint must be JSON object",
    },
    { "Unknown operation fails",
      "{ \"foo\": [] }",
      -1,
      "unknown constraint operator: foo",
    },
    { "non-array argument to 'and' fails",
      "{ \"and\": \"foo\" }",
      -1,
      "and operator value must be an array",
    },
    { "non-array argument to 'or' fails",
      "{ \"or\": \"foo\" }",
      -1,
      "or operator value must be an array",
    },
    { "non-array argument to 'properties' fails",
      "{ \"properties\": \"foo\" }",
      -1,
      "properties value must be an array",
    },
    { "non-string property fails",
      "{ \"properties\": [ \"foo\", 42 ] }",
      -1,
      "non-string property specified",
    },
    { "invalid property string fails",
      "{ \"properties\": [ \"foo\", \"bar&\" ] }",
      -1,
      "invalid character '&' in property \"bar&\"",
    },
    { "empty object is valid constraint",
      "{}",
      0,
      NULL
    },
    { "empty and object is valid constraint",
      "{ \"and\": [] }",
      0,
      NULL
    },
    { "empty or object is valid constraint",
      "{ \"or\": [] }",
      0,
      NULL
    },
    { "empty properties object is valid constraint",
      "{ \"properties\": [] }",
      0,
      NULL
    },
    { "complex conditional works",
      "{ \"and\": \
         [ { \"or\": \
             [ {\"properties\": [\"foo\"]}, \
               {\"properties\": [\"bar\"]}  \
             ] \
           }, \
           { \"and\": \
             [ {\"properties\": [\"xx\"]}, \
               {\"properties\": [\"yy\"]}  \
             ] \
           } \
         ] \
      }",
      0,
      NULL
    },
    { NULL, NULL, 0, NULL }
};

void test_match ()
{
    struct rnode *n;
    if (!(n = rnode_create ("foo0", 0, "0-3")))
        BAIL_OUT ("failed to create rnode object");

    ok (rnode_set_property (n, "xx") == 0,
        "rnode_set_property: foo0 has xx");
    ok (rnode_set_property (n, "yy") == 0,
        "rnode_set_property: foo0 has yy");

    struct match_test *t = match_tests;
    while (t->desc) {
        json_t *o = json_loads (t->json, 0, NULL);
        if (!o)
            BAIL_OUT ("failed to parse json logic for '%s'", t->desc);
        ok (rnode_match (n, o) == t->result, "%s", t->desc);
        json_decref (o);
        t++;
    }
    rnode_destroy (n);
}

void test_validate ()
{
    flux_error_t error;
    struct validate_test *t = validate_tests;
    while (t->desc) {
        json_t *o = json_loads (t->json, 0, NULL);
        if (!o)
            BAIL_OUT ("failed to parse json logic for '%s'", t->desc);
        int rc = rnode_match_validate (o, &error);
        ok (rc == t->rc, "%s", t->desc);
        if (rc != t->rc)
            diag ("%s", error.text);
        if (t->err)
            is (error.text, t->err, "got expected error: %s", error.text);
        json_decref (o);
        t++;
    }
}

void test_invalid ()
{
    json_t *o = json_object ();
    if (!o)
        BAIL_OUT ("test_invalid: json_object() failed");
    ok (!rnode_match (NULL, NULL),
        "rnode_match (NULL, NULL) returns false");
    ok (!rnode_match (NULL, o),
        "rnode_match (NULL, o) returns false");
    ok (rnode_match_validate (NULL, NULL) == -1,
        "rnode_match_validate (NULL, NULL) == -1");
    lives_ok ({rnode_match_validate (o, NULL); },
        "rnode_match_validate (o, NULL) doesn't crash");
    ok (rnode_copy_match (NULL, NULL) == NULL,
        "rnode_copy_match (NULL, NULL) returns NULL");
    json_decref (o);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);
    test_match ();
    test_validate ();
    test_invalid ();
    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
