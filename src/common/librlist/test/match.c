/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
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
    bool rc;
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
    { "hostlist operator works",
      "{\"hostlist\": [\"foo[0-2]\"]}",
      true,
    },
    { "hostlist operator works with non-matching hostlist",
      "{\"hostlist\": [\"foo[1-3]\"]}",
      false,
    },
    { "ranks operator works",
      "{\"ranks\": [\"0,2\", \"1\"]}",
      true,
    },
    { "ranks operator works with non-matching rank",
      "{\"ranks\": [\"1-3\"]}",
      false,
    },
    { NULL, NULL, false },
};

struct validate_test validate_tests[] = {
    { "non-object fails",
      "[]",
      false,
      "constraint must be JSON object",
    },
    { "Unknown operation fails",
      "{ \"foo\": [] }",
      false,
      "unknown constraint operator: foo",
    },
    { "non-array argument to 'and' fails",
      "{ \"and\": \"foo\" }",
      false,
      "and operator value must be an array",
    },
    { "non-array argument to 'or' fails",
      "{ \"or\": \"foo\" }",
      false,
      "or operator value must be an array",
    },
    { "non-array argument to 'properties' fails",
      "{ \"properties\": \"foo\" }",
      false,
      "properties value must be an array",
    },
    { "non-string property fails",
      "{ \"properties\": [ \"foo\", 42 ] }",
      false,
      "non-string property specified",
    },
    { "invalid property string fails",
      "{ \"properties\": [ \"foo\", \"bar&\" ] }",
      false,
      "invalid character '&' in property \"bar&\"",
    },
    { "empty object is valid constraint",
      "{}",
      true,
      NULL
    },
    { "empty and object is valid constraint",
      "{ \"and\": [] }",
      true,
      NULL
    },
    { "empty or object is valid constraint",
      "{ \"or\": [] }",
      true,
      NULL
    },
    { "empty properties object is valid constraint",
      "{ \"properties\": [] }",
      true,
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
      true,
      NULL
    },
    { "hostlist can be included",
      "{\"hostlist\": [\"foo[0-10]\"]}",
      true,
      NULL
    },
    { "invalid hostlist fails",
      "{\"hostlist\": [\"foo0-10]\"]}",
      false,
      "invalid hostlist 'foo0-10]' in [\"foo0-10]\"]",
    },
    { "ranks can be included",
      "{\"ranks\": [\"0-10\"]}",
      true,
      NULL
    },
    { "invalid ranks entry fails",
      "{\"ranks\": [\"5,1-3\"]}",
      false,
      "invalid idset '5,1-3' in [\"5,1-3\"]",
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
        flux_error_t error;
        struct job_constraint *c;
        json_t *o = json_loads (t->json, 0, NULL);
        if (!o)
            BAIL_OUT ("failed to parse json logic for '%s'", t->desc);
        if (!(c = job_constraint_create (o, &error)))
            BAIL_OUT ("%s: job_constraint_create: %s", t->desc, error.text);
        ok (rnode_match (n, c) == t->result, "%s", t->desc);
        json_decref (o);
        job_constraint_destroy (c);
        t++;
    }
    rnode_destroy (n);
}

void test_validate ()
{
    flux_error_t error;
    struct validate_test *t = validate_tests;
    while (t->desc) {
        struct job_constraint *c;
        json_t *o = json_loads (t->json, 0, NULL);
        if (!o)
            BAIL_OUT ("failed to parse json logic for '%s'", t->desc);
        bool result = ((c = job_constraint_create (o, &error)) != NULL);
        ok (result == t->rc, "%s", t->desc);
        if (result != t->rc)
            diag ("%s", error.text);
        if (t->err)
            is (error.text, t->err, "got expected error: %s", error.text);
        json_decref (o);
        job_constraint_destroy (c);
        t++;
    }
}

void test_invalid ()
{
    json_t *o = json_object ();
    struct job_constraint *c = job_constraint_create (o, NULL);
    if (!o || !c)
        BAIL_OUT ("test_invalid: json_object() failed");
    ok (!rnode_match (NULL, NULL),
        "rnode_match (NULL, NULL) returns false");
    ok (!rnode_match (NULL, c),
        "rnode_match (NULL, c) returns false");
    ok (rnode_copy_match (NULL, NULL) == NULL,
        "rnode_copy_match (NULL, NULL) returns NULL");
    json_decref (o);
    job_constraint_destroy (c);
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
