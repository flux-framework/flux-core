/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Test vectors from RFC 52, copied verbatim, with one string literal
 * per RFC line block.
 */
struct valid_vector {
    const char *body; // busctl(1) notation
    const char *params;
};

static const struct valid_vector valid_vectors[] = {
    { "",
      "[]" },
    { "yy 0 255",
      "[0,255]" },
    { "bb true false",
      "[true,false]" },
    { "nn -32768 32767",
      "[-32768,32767]" },
    { "qq 0 65535",
      "[0,65535]" },
    { "ii -2147483648 2147483647",
      "[-2147483648,2147483647]" },
    { "uu 0 4294967295",
      "[0,4294967295]" },
    { "xx -9223372036854775808 9223372036854775807",
      "[\"-9223372036854775808\",\"9223372036854775807\"]" },
    { "tt 0 18446744073709551615",
      "[\"0\",\"18446744073709551615\"]" },
    { "dd -1.5 0.25",
      "[-1.5,0.25]" },
    { "ss hello \"\"",
      "[\"hello\",\"\"]" },
    { "o /org/freedesktop/systemd1/unit/foo_2eservice",
      "[\"/org/freedesktop/systemd1/unit/foo_2eservice\"]" },
    { "g a{sv}",
      "[\"a{sv}\"]" },
    { "ai 0",
      "[[]]" },
    { "as 2 a b",
      "[[\"a\",\"b\"]]" },
    { "aas 2 1 a 0",
      "[[[\"a\"],[]]]" },
    { "(sasb) /bin/true 1 true false",
      "[[\"/bin/true\",[\"true\"],false]]" },
    { "a(ss) 2 /dev/null rw /dev/zero r",
      "[[[\"/dev/null\",\"rw\"],[\"/dev/zero\",\"r\"]]]" },
    { "a{ss} 0",
      "[{}]" },
    { "a{sv} 2 A s x B t 18446744073709551615",
      "[{\"A\":[\"s\",\"x\"],\"B\":[\"t\",\"18446744073709551615\"]}]" },
    { "a{sa{sv}} 1 eth0 1 mtu u 1500",
      "[{\"eth0\":{\"mtu\":[\"u\",1500]}}]" },
    { "a{uv} 2 7 b true 3 s x",
      "[[[7,[\"b\",true]],[3,[\"s\",\"x\"]]]]" },
    { "a{ts} 1 18446744073709551615 max",
      "[[[\"18446744073709551615\",\"max\"]]]" },
    { "v v b false",
      "[[\"v\",[\"b\",false]]]" },
    { "av 2 i 1 (ss) a b",
      "[[[\"i\",1],[\"(ss)\",[\"a\",\"b\"]]]]" },
    { "ssa(sv)a(sa(sv)) shell-1.service fail"
      " 2 Description s hi"
      " ExecStart a(sasb) 1 /bin/sleep 2 sleep 60 false"
      " 0",
      "[\"shell-1.service\",\"fail\","
      " [[\"Description\",[\"s\",\"hi\"]],"
      " [\"ExecStart\",[\"a(sasb)\","
      " [[\"/bin/sleep\",[\"sleep\",\"60\"],false]]]]],"
      " []]" },
    { "sa{sv}as org.freedesktop.systemd1.Service"
      " 1 ExecStart a(sasbttttuii) 1"
      " /bin/sleep 2 sleep 60 false"
      " 1759771234567890 81234567 0 0"
      " 4242 0 0"
      " 0",
      "[\"org.freedesktop.systemd1.Service\","
      " {\"ExecStart\":[\"a(sasbttttuii)\","
      " [[\"/bin/sleep\",[\"sleep\",\"60\"],false,"
      " \"1759771234567890\",\"81234567\",\"0\",\"0\","
      " 4242,0,0]]]},"
      " []]" },
};

struct invalid_params_vector {
    const char *signature;
    const char *params;
    const char *reason;
};

static const struct invalid_params_vector invalid_params_vectors[] = {
    { "ss", "[\"one\"]", "too few values" },
    { "s", "[\"one\",\"two\"]", "too many values" },
    { "i", "[\"42\"]", "string for integer" },
    { "i", "[1.5]", "real for integer" },
    { "d", "[\"1.5\"]", "string for double" },
    { "b", "[1]", "integer for boolean" },
    { "s", "[null]", "null for string" },
    { "y", "[256]", "out of range" },
    { "q", "[-1]", "out of range" },
    { "u", "[4294967296]", "out of range" },
    { "t", "[42]", "integer for 64-bit integer" },
    { "t", "[\"-1\"]", "out of range" },
    { "t", "[\"18446744073709551616\"]", "out of range" },
    { "x", "[\"9223372036854775808\"]", "out of range" },
    { "t", "[\"007\"]", "leading zero" },
    { "x", "[\"+5\"]", "leading +" },
    { "x", "[\"-0\"]", "negative zero" },
    { "t", "[\" 5\"]", "whitespace" },
    { "t", "[\"0x10\"]", "not decimal" },
    { "t", "[\"\"]", "empty string" },
    { "s", "[\"a\\u0000b\"]", "contains U+0000" },
    { "o", "[\"not/a/path\"]", "invalid object path" },
    { "g", "[\"(\"]", "invalid signature" },
    { "g", "[\"{sv}\"]", "dict entry outside array" },
    { "h", "[5]", "integer for descriptor object" },
    { "h", "[{\"fd\":-1,\"pid\":1}]", "negative descriptor" },
    { "h", "[{\"pid\":1}]", "missing fd" },
    { "h", "[{\"fd\":5}]", "missing pid" },
    { "h", "[{\"fd\":5,\"pid\":0}]", "invalid pid" },
    { "(si)", "[[\"x\"]]", "too few struct members" },
    { "as", "[\"a\"]", "string for array" },
    { "a{sv}", "[[]]", "array for string-key dict" },
    { "a{uv}", "[{}]", "object for non-string-key dict" },
    { "a{uv}", "[[[7]]]", "dict entry without value" },
    { "{sv}", "[{}]", "dict entry outside array" },
    { "a{vs}", "[[]]", "dict key is not a basic type" },
    { "a{svs}", "[{}]", "dict entry with three members" },
    { "v", "[[\"s\"]]", "variant without value" },
    { "v", "[[\"s\",\"x\",\"y\"]]", "variant with extra element" },
    { "v", "[[\"ss\",[\"a\",\"b\"]]]", "variant signature with two types" },
    { "v", "[[\"(s\",[\"x\"]]]", "malformed variant signature" },
};

struct invalid_body_vector {
    const char *body; // busctl(1) notation
    const char *reason;
};

static const struct invalid_body_vector invalid_body_vectors[] = {
    { "a{sv} 2 A s x A s y", "duplicate dict key" },
    { "d nan", "NaN" },
    { "d inf", "infinite" },
    { "d -inf", "infinite" },
    { "h 5", "file descriptors cannot be encoded" },
};
