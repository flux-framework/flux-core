#!/usr/bin/env python3
###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.constraint.parser import ConstraintParser, ConstraintSyntaxError
from pycotap import TAPTestRunner

VALID = [
    {"in": "a", "out": {"t": ["a"]}},
    {"in": "(a)", "out": {"t": ["a"]}},
    {"in": "( a )", "out": {"t": ["a"]}},
    {"in": "'a'", "out": {"t": ["a"]}},
    {"in": "('a')", "out": {"t": ["a"]}},
    {"in": "a|b", "out": {"or": [{"t": ["a"]}, {"t": ["b"]}]}},
    {"in": "a or b", "out": {"or": [{"t": ["a"]}, {"t": ["b"]}]}},
    {"in": "a || b", "out": {"or": [{"t": ["a"]}, {"t": ["b"]}]}},
    {"in": "a||b", "out": {"or": [{"t": ["a"]}, {"t": ["b"]}]}},
    {"in": "a b c", "out": {"and": [{"t": ["a"]}, {"t": ["b"]}, {"t": ["c"]}]}},
    {"in": "a&b&c", "out": {"and": [{"t": ["a"]}, {"t": ["b"]}, {"t": ["c"]}]}},
    {"in": "a and b and c", "out": {"and": [{"t": ["a"]}, {"t": ["b"]}, {"t": ["c"]}]}},
    {"in": "a && b && c", "out": {"and": [{"t": ["a"]}, {"t": ["b"]}, {"t": ["c"]}]}},
    {
        "in": "a&b|c",
        "out": {"or": [{"and": [{"t": ["a"]}, {"t": ["b"]}]}, {"t": ["c"]}]},
    },
    {
        "in": "a&(b|c)",
        "out": {"and": [{"t": ["a"]}, {"or": [{"t": ["b"]}, {"t": ["c"]}]}]},
    },
    {
        "in": "(a&(b|c))",
        "out": {"and": [{"t": ["a"]}, {"or": [{"t": ["b"]}, {"t": ["c"]}]}]},
    },
    {
        "in": "(a&(b|-c))",
        "out": {"and": [{"t": ["a"]}, {"or": [{"t": ["b"]}, {"not": [{"t": ["c"]}]}]}]},
    },
    {
        "in": "a or host:foo",
        "out": {"or": [{"t": ["a"]}, {"host": ["foo"]}]},
    },
    {
        "in": "a or -host:foo",
        "out": {"or": [{"t": ["a"]}, {"not": [{"host": ["foo"]}]}]},
    },
    {
        "in": "a or time:11:00am",
        "out": {"or": [{"t": ["a"]}, {"time": ["11:00am"]}]},
    },
    {
        "in": "a or test:'a quoted string'",
        "out": {"or": [{"t": ["a"]}, {"test": ["a quoted string"]}]},
    },
    {
        "in": "a or name:'/foo|(bar)|baz.*/'",
        "out": {"or": [{"t": ["a"]}, {"name": ["/foo|(bar)|baz.*/"]}]},
    },
]

INVALID = ["a|", "a:' ", "(a", "(a))", "-(a|b)", "4g,host:foo", "foo and a:"]


class TestConstraintParser(ConstraintParser):
    operator_map = {None: "t", "a": "op-a"}


class TestSplitParser(ConstraintParser):
    operator_map = {None: "t"}
    split_values = {"t": ","}


class TestCombineParser(ConstraintParser):
    operator_map = {None: "t"}
    combined_terms = set("t")


class TestParser(unittest.TestCase):
    def test_parse_valid(self):
        parser = TestConstraintParser()
        for test in VALID:
            print(f"checking `{test['in']}'")
            result = parser.parse(test["in"])
            self.assertDictEqual(test["out"], result)

    def test_parse_invalid(self):
        parser = TestConstraintParser()
        for test in INVALID:
            print(f"checking invalid input `{test}'")
            with self.assertRaises((SyntaxError, ConstraintSyntaxError)):
                parser.parse(test)

    def test_default_parser(self):
        parser = ConstraintParser()
        print("ConstraintParser requires operator by default")
        with self.assertRaises(ConstraintSyntaxError):
            parser.parse("foo")

    def test_split_values(self):
        parser = TestSplitParser()
        print("ConstraintParser can split values if configured")
        result = parser.parse("xx,yy")
        print(f"got {result}")
        self.assertDictEqual({"t": ["xx", "yy"]}, result)

        result = parser.parse("xx")
        print(f"got {result}")
        self.assertDictEqual({"t": ["xx"]}, result)

        result = parser.parse("t:xx,yy")
        print(f"got {result}")
        self.assertDictEqual({"t": ["xx", "yy"]}, result)

    def test_combine_terms(self):
        parser = TestCombineParser()
        print("ConstraintParser doesn't combine unconfigured like terms")
        result = parser.parse("a:xx b:yy")
        print(f"got {result}")
        self.assertDictEqual({"and": [{"a": ["xx"]}, {"b": ["yy"]}]}, result)

        print("ConstraintParser combines configured like terms")
        result = parser.parse("xx and yy")
        print(f"got {result}")
        self.assertDictEqual({"t": ["xx", "yy"]}, result)

        print("ConstraintParser combines configured like terms (nested)")
        result = parser.parse("not (xx and yy)")
        print(f"got {result}")
        self.assertDictEqual({"not": [{"t": ["xx", "yy"]}]}, result)

        print("ConstraintParser doesn't combine like terms (or)")
        result = parser.parse("xx|yy")
        print(f"got {result}")
        self.assertDictEqual({"or": [{"t": ["xx"]}, {"t": ["yy"]}]}, result)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
