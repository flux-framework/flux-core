/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <stdbool.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjob/count.h"
#include "ccan/str/str.h"

struct inout {
    const char *in;
    int flags;
    const char *out;
};

struct inout test_codec_inputs[] = {
    { "2",              0,          "2" },
    { "7-9",            0,          "7,8,9" },
    { "1,7-9",          0,          "1,7,8,9" },
    { "1,7-9,16",       0,          "1,7,8,9,16" },
    { "1,7-9,14,16",    0,          "1,7,8,9,14,16" },
    { "1-3,7-9,14,16",  0,          "1,2,3,7,8,9,14,16" },
    { "2,3,4,5",        0,          "2,3,4,5" },
    { "1048576",        0,          "1048576"},

    { "[2]",            0,          "2" },
    { "[7-9]",          0,          "7,8,9" },
    { "[2,3,4,5]",      0,          "2,3,4,5" },

    { "2",              COUNT_FLAG_SHORT,  "2" },
    { "7-9",            COUNT_FLAG_SHORT,  "7-9" },
    { "1,7-9",          COUNT_FLAG_SHORT,  "1,7-9" },
    { "1,7-9,16",       COUNT_FLAG_SHORT,  "1,7-9,16" },
    { "1,7-9,14,16",    COUNT_FLAG_SHORT,  "1,7-9,14,16" },
    { "1-3,7-9,14,16",  COUNT_FLAG_SHORT,  "1-3,7-9,14,16" },
    { "2,3,4,5",        COUNT_FLAG_SHORT,  "2-5" },

    { "2",              COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "2" },
    { "7-9",            COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[7-9]" },
    { "1,7-9",          COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[1,7-9]" },
    { "1,7-9,16",       COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[1,7-9,16]" },
    { "1,7-9,14,16",    COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[1,7-9,14,16]" },
    { "1-3,7-9,14,16",  COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[1-3,7-9,14,16]"},
    { "2,3,4,5",        COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[2-5]" },

    /* ranges */
    { "3-3",            0,                                    "3" },
    { "2+",             0,                                    "2+:1:+" },
    { "2-5:3",          0,                                    "2-5:3:+"},
    { "2-5:1:+",        0,                                    "2-5:1:+"},
    { "2-8:2:*",        0,                                    "2-8:2:*" },
    { "25-133:17:+",    0,                                    "25-133:17:+"},
    { "[3-3]",          0,                                    "3" },
    { "[2+]",           0,                                    "2+:1:+" },
    { "[2-5:3]",        0,                                    "2-5:3:+"},
    { "[2-5:1:+]",      0,                                    "2-5:1:+"},
    { "[2-8:2:*]",      0,                                    "2-8:2:*" },
    { "[25-133:17:+]",  0,                                    "25-133:17:+"},
    { "3-3",            COUNT_FLAG_SHORT,                     "3" },
    { "2+",             COUNT_FLAG_SHORT,                     "2+" },
    { "2-5:3",          COUNT_FLAG_SHORT,                     "2-5:3" },
    { "2-5:1:+",        COUNT_FLAG_SHORT,                     "2-5" },
    { "2-8:2:*",        COUNT_FLAG_SHORT,                     "2-8:2:*" },
    { "25-133:17:+",    COUNT_FLAG_SHORT,                     "25-133:17"},
    { "3-3",            COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "3" },
    { "2+",             COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[2+]" },
    { "2-5:3",          COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[2-5:3]" },
    { "2-5:1:+",        COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[2-5]" },
    { "2-8:2:*",        COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[2-8:2:*]" },
    { "25-133:17:+",    COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS, "[25-133:17]"},

    /* ranges as JSON strings */
    { "{\"min\": 3, \"max\": 3}",                                      0, "3" },
    { "{\"min\": 2}",                                                  0, "2+:1:+" },
    { "{\"min\": 2, \"max\": 5, \"operand\": 3}",                      0, "2-5:3:+" },
    { "{\"min\": 2, \"max\": 5, \"operand\": 1, \"operator\": \"+\"}", 0, "2-5:1:+" },
    { "{\"min\": 2, \"max\": 8, \"operand\": 2, \"operator\": \"*\"}", 0, "2-8:2:*" },
    { "{\"min\": 25, \"max\": 133, \"operand\": 17, \"operator\": \"+\"}",
      0,
      "25-133:17:+" },
    { "{\"min\": 3, \"max\": 3}",
      COUNT_FLAG_SHORT,
      "3" },
    { "{\"min\": 2}",
      COUNT_FLAG_SHORT,
      "2+" },
    { "{\"min\": 2, \"max\": 5, \"operand\": 3}",
      COUNT_FLAG_SHORT,
      "2-5:3" },
    { "{\"min\": 2, \"max\": 5, \"operand\": 1, \"operator\": \"+\"}",
      COUNT_FLAG_SHORT,
      "2-5" },
    { "{\"min\": 2, \"max\": 8, \"operand\": 2, \"operator\": \"*\"}",
      COUNT_FLAG_SHORT,
      "2-8:2:*" },
    { "{\"min\": 25, \"max\": 133, \"operand\": 17, \"operator\": \"+\"}",
      COUNT_FLAG_SHORT,
      "25-133:17" },
    { "{\"min\": 3, \"max\": 3}",
      COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS,
      "3" },
    { "{\"min\": 2}",
      COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS,
      "[2+]" },
    { "{\"min\": 2, \"max\": 5, \"operand\": 3}",
      COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS,
      "[2-5:3]" },
    { "{\"min\": 2, \"max\": 5, \"operand\": 1, \"operator\": \"+\"}",
      COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS,
      "[2-5]" },
    { "{\"min\": 2, \"max\": 8, \"operand\": 2, \"operator\": \"*\"}",
      COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS,
      "[2-8:2:*]" },
    { "{\"min\": 25, \"max\": 133, \"operand\": 17, \"operator\": \"+\"}",
      COUNT_FLAG_SHORT|COUNT_FLAG_BRACKETS,
      "[25-133:17]" },

    /* expected failures */
    { "2-8:1:+",        0xffff,     NULL },
    { "",               0,          NULL },
    { "[]",             0,          NULL },
    { "[0]",            0,          NULL },
    { "{}",             0,          NULL },
    { "3-2:1:+",        0,          NULL },
    { "2-8:01",         0,          NULL },
    { "2-8:1;+",        0,          NULL },
    { "2-8:1:*",        0,          NULL },
    { "2-8:1:^",        0,          NULL },
    { "2-8:1:/",        0,          NULL },
    { "2-8:1:++",       0,          NULL },
    { "4.2",            0,          NULL },
    { "x",              0,          NULL },
    { "1-2x",           0,          NULL },
    { "01,2",           0,          NULL },
    { "00",             0,          NULL },
    { "3,2",            0,          NULL },
    { "3-0",            0,          NULL },
    { "2,2,2,2",        0,          NULL },
    { "[0",             0,          NULL },
    { "0]",             0,          NULL },
    { "[[0]]",          0,          NULL },
    { "[[0,2]",         0,          NULL },
    { "[0,2]]",         0,          NULL },
    { "0,[2",           0,          NULL },
    { "0]2",            0,          NULL },
    { "0-",             0,          NULL },
    { "[0-]",           0,          NULL },
    { "-5",             0,          NULL },
    { "[-5]",           0,          NULL },
    { "{\"man\": 4}",   0,          NULL },
    { "{\"min\": 4, \"max\": 6, \"operand\": 0, \"operator\": \"+\"}",   0, NULL },
    { "{\"min\": 4, \"max\": 6, \"operand\": -1, \"operator\": \"+\"}",  0, NULL },
    { "{\"min\": -2, \"max\": 6, \"operand\": 1, \"operator\": \"+\"}",  0, NULL },
    { "{\"min\": 3, \"max\": 1, \"operand\": 2, \"operator\": \"+\"}",   0, NULL },
    { "{\"min\": 2, \"max\": 16, \"operand\": 1, \"operator\": \"*\"}",  0, NULL },
    { "{\"min\": 2, \"max\": 16, \"operand\": 1, \"operator\": \"^\"}",  0, NULL },
    { "{\"min\": 1, \"max\": 16, \"operand\": 2, \"operator\": \"^\"}",  0, NULL },
    { "{\"min\": 2, \"max\": 16, \"operand\": 1, \"operator\": \"/\"}",  0, NULL },
    { "{[\"min\": 4, \"max\": 6, \"operand\": 1, \"operator\": \"+\"]}", 0, NULL },

    { NULL, 0, NULL },
};

void test_codec (void)
{
    struct inout *ip;

    for (ip = &test_codec_inputs[0]; ip->in != NULL; ip++) {
        struct count *count;

        errno = 0;
        count = count_decode (ip->in);
        if (ip->out == NULL) { // expected fail
            if (count != NULL) {
                char *s = count_encode (count, ip->flags);
                ok (s == NULL && errno == EINVAL,
                    "count_encode flags=0x%x '%s' fails with EINVAL",
                    ip->flags, ip->in);
                free (s);
            } else {
                ok (count == NULL && errno == EINVAL,
                    "count_decode '%s' fails with EINVAL",
                    ip->in);
            }
        }
        else {
            ok (count != NULL,
                "count_decode '%s' works", ip->in);
            if (count != NULL) {
                char *s = count_encode (count, ip->flags);
                bool match = (s && streq (s, ip->out));
                ok (match == true,
                    "count_encode flags=0x%x '%s'->'%s' works",
                    ip->flags, ip->in, ip->out);
                if (!match)
                    diag ("%s", s ? s : "NULL");
                free (s);
            }
        }
        count_destroy (count);
    }
}

struct inout test_iteration_inputs[] = {
    /* wrap the JSON in an array to also allow integer and string types */
    { "[1]", 0, "1" },
    { "[\"13\"]", 0, "13" },
    { "[\"5,7,13\"]", 0, "5,7,13" },
    { "[{\"min\": 4, \"max\": 6}]", 0, "4,5,6" },
    { "[{\"min\": 4, \"max\": 6, \"operand\": 1, \"operator\": \"+\"}]", 0, "4,5,6" },
    { "[{\"min\": 1, \"max\": 3, \"operand\": 2}]", 0, "1,3" },
    { "[{\"min\": 1, \"max\": 3, \"operand\": 2, \"operator\": \"+\"}]", 0, "1,3" },
    { "[{\"min\": 2, \"max\": 16, \"operand\": 2, \"operator\": \"*\"}]", 0, "2,4,8,16" },
    { "[{\"min\": 2, \"max\": 16, \"operand\": 2, \"operator\": \"^\"}]", 0, "2,4,16" },

    /* expected failures */
    { "[-1]", 0, NULL },
    { "[\"13-\"]", 0, NULL },
    
    { NULL, 0, NULL },
};

void test_iteration (void)
{
    struct inout *ip;
    char s[256];

    for (ip = &test_iteration_inputs[0]; ip->in != NULL; ip++) {
        json_error_t error;
        json_t *json_in;
        struct count *count;
        
        if (!(json_in = json_loads (ip->in, 0, &error))) {
            diag ("json_loads '%s' fails with error '%s'", ip->in, error.text);
            continue;
        }
        /* extract first element of array to get actual JSON of interest */
        count = count_create (json_array_get (json_in, 0), &error);
        if (ip->out == NULL) { // expected fail
            ok (count == NULL,
                "count_create '%s' fails with error '%s'",
                ip->in, error.text);
        }
        else {
            ok (count != NULL,
                "count_create '%s' works", ip->in);
            if (!count)
                diag ("error '%s'", error.text);
            if (count != NULL) {
                int i = 0;
                int value = count_first (count);
                while (value != COUNT_INVALID_VALUE) {
                    i += sprintf (s+i, "%u,", value);
                    value = count_next (count, value);
                }
                s[i-1] = '\0';
                bool match = streq (s, ip->out);
                ok (match == true,
                    "count iteration '%s'->'%s' works",
                    ip->in, ip->out);
                if (!match)
                    diag ("%s", s);
            }
        }
        count_destroy (count);
        json_decref (json_in);
    }
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_codec ();
    test_iteration ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
