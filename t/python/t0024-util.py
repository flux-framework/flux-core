#!/usr/bin/env python3

###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import os
import sys
import time
import unittest
from collections import namedtuple
from datetime import datetime
from unittest.mock import patch

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.util import (
    ColorAction,
    OutputFormat,
    UtilDatetime,
    del_treedict,
    parse_datetime,
    set_treedict,
)
from pycotap import TAPTestRunner


def ts(year, month, day, hour=0, minute=0, sec=0, us=0):
    return datetime(year, month, day, hour, minute, sec, us).astimezone().timestamp()


class TestParseDatetime(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.now = datetime(2021, 6, 10, 8, 0, 0)
        self.ts = self.now.timestamp()

    def parse(self, string):
        return parse_datetime(string, self.now).timestamp()

    def parsedt(self, string):
        return parse_datetime(string).astimezone()

    def test_now(self):
        # "now" returns parse_datetime(now=) when set
        self.assertEqual(self.parse("now"), self.ts)
        # "now" has better than second precision
        now = parse_datetime("now").timestamp()
        self.assertNotEqual(now % 1, 0)

    def test_fsd(self):
        self.assertEqual(self.parse("+1"), self.ts + 1)
        self.assertEqual(self.parse("-1"), self.ts - 1)
        self.assertEqual(self.parse("+1m"), self.ts + 60)
        self.assertEqual(self.parse("+1.5m"), self.ts + 90)
        self.assertEqual(self.parse("+1h"), self.ts + 3600)
        self.assertEqual(self.parse("+100ms"), self.ts + 0.1)
        self.assertEqual(self.parse("+1ms"), self.ts + 0.001)

    def test_fsd_invalid(self):
        with self.assertRaises(ValueError):
            self.parse("+1ns")
        with self.assertRaises(ValueError):
            self.parse("+1x")
        with self.assertRaises(ValueError):
            self.parse("+x")
        with self.assertRaises(ValueError):
            self.parse("-")

    def test_basic_datetime(self):
        self.assertEqual(self.parse("6/10/2021 08:00"), self.ts)
        self.assertEqual(self.parse("2021-06-10 08:00:00"), self.ts)
        self.assertEqual(self.parse("Jun 10, 2021 8am"), self.ts)

    def test_basic_invalid(self):
        with self.assertRaises(ValueError):
            self.parse("blursday")
        with self.assertRaises(ValueError):
            self.parse("6/45/2021")

    def test_nlp(self):
        self.assertEqual(self.parse("in 10 min"), self.ts + 600)
        self.assertEqual(self.parse("tomorrow"), ts(2021, 6, 11))
        self.assertEqual(self.parse("noon tomorrow"), ts(2021, 6, 11, 12))
        self.assertEqual(self.parse("last wed"), ts(2021, 6, 9))


class TestUtilDatetime(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.zero = UtilDatetime.fromtimestamp(0.0)
        self.ts = UtilDatetime(2021, 6, 10, 8, 0, 0)

    def test_zero(self):
        self.assertEqual(f"{self.zero}", "")
        self.assertEqual(f"{self.zero:::h}", "-")
        self.assertEqual(f"{self.zero:%FT%T::<6}", "      ")
        self.assertEqual(f"{self.zero:%FT%T::<6h}", "-     ")

    def test_fmt(self):
        self.assertEqual(f"{self.ts}", "2021-06-10T08:00:00")
        self.assertEqual(f"{self.ts:::h}", "2021-06-10T08:00:00")
        self.assertEqual(f"{self.ts:%b%d %R}", "Jun10 08:00")
        self.assertEqual(f"{self.ts:%b%d %R::h}", "Jun10 08:00")
        self.assertEqual(f"{self.ts:%b%d %R::>12}", " Jun10 08:00")
        self.assertEqual(f"{self.ts:%b%d %R::>12h}", " Jun10 08:00")


class Item:
    def __init__(self, s="foo", i=42, f=1234.567, d=978336000.0):
        self.s = s
        self.i = i
        self.f = f
        self.d = d


TestData = namedtuple(
    "TestData",
    (
        "input",
        "fmt",
        "hdrfmt",
        "fields",
        "header",
        "result",
    ),
)


class TestOutputFormat(unittest.TestCase):
    @classmethod
    def setUpClass(self):

        # Set timezone to UTC so rendered datetimes match expected value
        os.environ["TZ"] = "UTC"
        time.tzset()

        self.headings = {
            "s": "STRING",
            "i": "INTEGER",
            "f": "FLOAT",
            "d": "DATETIME",
        }
        self.item = Item()
        self.cases = [
            TestData("", "", "", [], "", ""),
            TestData("{s}", "{0.s}", "{s}", ["s"], "STRING", "foo"),
            TestData("{i}", "{0.i}", "{i}", ["i"], "INTEGER", "42"),
            TestData("{f}", "{0.f}", "{f}", ["f"], "FLOAT", "1234.567"),
            TestData("{d}", "{0.d}", "{d}", ["d"], "DATETIME", "978336000.0"),
            TestData(
                "{s:<6} {i:>7d}",
                "{0.s:<6} {0.i:>7d}",
                "{s:<6} {i:>7}",
                ["s", "i"],
                "STRING INTEGER",
                "foo         42",
            ),
            TestData(
                "{s:<6} {f:>8.2f}",
                "{0.s:<6} {0.f:>8.2f}",
                "{s:<6} {f:>8}",
                ["s", "f"],
                "STRING    FLOAT",
                "foo     1234.57",
            ),
            TestData(
                "a {s:<6} b{f:>8.2f} ",
                "a {0.s:<6} b{0.f:>8.2f} ",
                "a {s:<6} b{f:>8} ",
                ["s", "f"],
                "a STRING b   FLOAT ",
                "a foo    b 1234.57 ",
            ),
            TestData(
                "{i:>7d} {d!d:%b%d %R::^20}",
                "{0.i:>7d} {0.d!d:%b%d %R::^20}",
                "{i:>7} {d:^20}",
                ["i", "d"],
                "INTEGER       DATETIME      ",
                "     42     Jan01 08:00     ",
            ),
            TestData(
                "sort:i,-d {i:>7d} {d!d:%b%d %R::^20}",
                "{0.i:>7d} {0.d!d:%b%d %R::^20}",
                "{i:>7} {d:^20}",
                ["i", "d"],
                "INTEGER       DATETIME      ",
                "     42     Jan01 08:00     ",
            ),
        ]

    def test_basic(self):
        for t in self.cases:
            fmt = OutputFormat(t.input, headings=self.headings)
            self.assertEqual(fmt.get_format(include_sort_prefix=False), t.fmt)
            self.assertEqual(fmt.header_format(), t.hdrfmt)
            self.assertCountEqual(fmt.fields, t.fields)
            self.assertEqual(fmt.header(), t.header)
            self.assertEqual(fmt.format(self.item), t.result)

    def test_filter(self):
        a = Item("", 0, 2.2)
        d = Item("", 1, 1.1)
        z = Item("", 23456789, 1.0)

        items = [a, d, z]
        fmt = OutputFormat(
            "?+:{i:>7} ?:{s:>6} ?:{f:8.2}", headings=self.headings
        ).filter(items)
        self.assertEqual(fmt, "{i:>8} {f:8.2}")

        # N.B. float has a width of 5 due to "FLOAT" header
        fmt = OutputFormat(
            "?+:{i:>7} ?:{s:>6} ?+:{f:.2f}", headings=self.headings
        ).filter(items)
        self.assertEqual(fmt, "{i:>8} {f:5.2f}")

        fmt = OutputFormat(
            "?+:{i:>7} ?:{s:>6} ?+:{f:.2f}", headings=self.headings
        ).filter(items, no_header=True)
        self.assertEqual(fmt, "{i:>8} {f:3.2f}")

    def test_sort(self):
        a = Item("a", 0, 2.2)
        d = Item("d", 33, 1.1)
        z = Item("z", 11, 1.0)

        items = [z, a, d]
        formatter = OutputFormat("{s}:{i}:{f}", headings=self.headings)
        formatter.set_sort_keys("s")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, d, z])

        items = [z, a, d]
        formatter.set_sort_keys("-s")
        formatter.sort_items(items)
        self.assertListEqual(items, [z, d, a])

        items = [z, a, d]
        formatter.set_sort_keys("i")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, z, d])

        items = [z, a, d]
        formatter.set_sort_keys("f")
        formatter.sort_items(items)
        self.assertListEqual(items, [z, d, a])

        # Embedded sort prefix works
        items = [z, a, d]
        formatter = OutputFormat("sort:i {s}:{i}:{f}", headings=self.headings)
        formatter.sort_items(items)
        self.assertListEqual(items, [a, z, d])

        # Embedded sort prefix can be overridden
        items = [z, a, d]
        formatter = OutputFormat("sort:i {s}:{i}:{f}", headings=self.headings)
        formatter.set_sort_keys("f")
        formatter.sort_items(items)
        self.assertListEqual(items, [z, d, a])

    def test_issue6530(self):
        a = Item("1234567890", 0, 2.2)
        b = Item("abcdefghijklmnop", 2, 13)
        c = Item("c", 4, 5.0)

        # N.B. iinteger has a width of 7 due to "INTEGER" header
        # N.B. float has a width of 5 due to "FLOAT" header
        items = [a, b, c]
        fmt = OutputFormat(
            "+:{s:5.5} +:{i:4d} +:{f:.2f}", headings=self.headings
        ).filter(items)
        self.assertEqual(fmt, "{s:16.16} {i:7d} {f:5.2f}")

        fmt = OutputFormat(
            "+:{s:5.5} +:{i:4d} +:{f:.2f}", headings=self.headings
        ).filter(items, no_header=True)
        self.assertEqual(fmt, "{s:16.16} {i:4d} {f:3.2f}")

    def test_copy(self):
        original = "+:{s:5.5} {i:4d} {f:.2f}"

        fmt = OutputFormat(original, headings=self.headings)
        self.assertEqual(fmt.get_format_prepended(""), original)

        # copy preserves original fmt
        self.assertEqual(fmt.copy().get_format_prepended(""), original)

        # except_fields can remove fields
        self.assertEqual(
            fmt.copy(except_fields=["s", "f"]).get_format_prepended(""), " {i:4d}"
        )

        # nullify_expansion removes formatting on +: fields
        self.assertEqual(
            fmt.copy(nullify_expansion=True).get_format_prepended(""),
            "{s} {i:4d} {f:.2f}",
        )

    def test_del_treedict(self):
        d = {}
        set_treedict(d, "a.b.c", 42)
        self.assertEqual(d, {"a": {"b": {"c": 42}}})

        del_treedict(d, "a.b.c")
        self.assertEqual(d, {"a": {"b": {}}})

        set_treedict(d, "a.b.c", 42)
        del_treedict(d, "a.b.c", remove_empty=True)
        self.assertEqual(d, {})

        set_treedict(d, "a.b.c", 42)
        del_treedict(d, "a.b")
        self.assertEqual(d, {"a": {}})

        # missing key raises KeyError
        set_treedict(d, "a.b.c", 42)
        with self.assertRaises(KeyError):
            del_treedict(d, "a.b.d")


class TestColorAction(unittest.TestCase):

    def make_parser(self):
        p = argparse.ArgumentParser()
        p.add_argument("--color", action=ColorAction, metavar="WHEN")
        return p

    def parse(self, args):
        return self.make_parser().parse_args(args)

    def test_default_is_auto(self):
        args = self.parse([])
        self.assertEqual(args.color, "auto")

    def test_default_no_color(self):
        with patch.dict(os.environ, {"NO_COLOR": "1"}):
            args = self.parse([])
        self.assertEqual(args.color, "never")

    def test_default_no_color_empty_ignored(self):
        with patch.dict(os.environ, {"NO_COLOR": ""}):
            args = self.parse([])
        self.assertEqual(args.color, "auto")

    def test_always(self):
        args = self.parse(["--color=always"])
        self.assertEqual(args.color, "always")
        self.assertTrue(args.color.enabled)

    def test_never(self):
        args = self.parse(["--color=never"])
        self.assertEqual(args.color, "never")
        self.assertFalse(args.color.enabled)

    def test_no_argument_implies_always(self):
        args = self.parse(["--color"])
        self.assertEqual(args.color, "always")
        self.assertTrue(args.color.enabled)

    def test_always_overrides_no_color(self):
        with patch.dict(os.environ, {"NO_COLOR": "1"}):
            args = self.parse(["--color=always"])
        self.assertTrue(args.color.enabled)

    def test_auto_tty(self):
        with patch.object(sys.stdout, "isatty", return_value=True):
            args = self.parse(["--color=auto"])
            self.assertTrue(args.color.enabled)

    def test_auto_no_tty(self):
        with patch.object(sys.stdout, "isatty", return_value=False):
            args = self.parse(["--color=auto"])
            self.assertFalse(args.color.enabled)

    def test_default_auto_tty(self):
        with patch.object(sys.stdout, "isatty", return_value=True):
            args = self.parse([])
            self.assertTrue(args.color.enabled)

    def test_default_auto_no_tty(self):
        with patch.object(sys.stdout, "isatty", return_value=False):
            args = self.parse([])
            self.assertFalse(args.color.enabled)

    def test_invalid_argument(self):
        with self.assertRaises(SystemExit):
            self.parse(["--color=bogus"])


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
