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
import math
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
    DisplayValue,
    OutputFormat,
    UtilDatetime,
    UtilFormatter,
    del_treedict,
    parse_datetime,
    parse_timestamp,
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

    def test_iso8601_basic_date(self):
        # ISO 8601 basic format calendar date (no hyphens)
        self.assertEqual(self.parse("20210610"), ts(2021, 6, 10))
        self.assertEqual(self.parse("20200229"), ts(2020, 2, 29))
        self.assertEqual(self.parse("20211231"), ts(2021, 12, 31))

    def test_iso8601_basic_date_invalid(self):
        with self.assertRaises(ValueError):
            self.parse("20211301")  # month 13
        with self.assertRaises(ValueError):
            self.parse("20210632")  # day 32
        with self.assertRaises(ValueError):
            self.parse("20210229")  # not a leap year
        with self.assertRaises(ValueError):
            self.parse("20210600")  # day 0
        with self.assertRaises(ValueError):
            self.parse("2021061")  # 7 digits
        with self.assertRaises(ValueError):
            self.parse("202106100")  # 9 digits
        with self.assertRaises(ValueError):
            self.parse("20210610.5")  # floating point
        with self.assertRaises(ValueError):
            self.parse("2021061.0")  # floating point

    def test_nlp(self):
        self.assertEqual(self.parse("in 10 min"), self.ts + 600)
        self.assertEqual(self.parse("tomorrow"), ts(2021, 6, 11))
        self.assertEqual(self.parse("noon tomorrow"), ts(2021, 6, 11, 12))
        self.assertEqual(self.parse("last wed"), ts(2021, 6, 9))


class TestParseTimestamp(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.now = datetime(2021, 6, 10, 8, 0, 0)
        self.ts = self.now.timestamp()

    def parse(self, string):
        return parse_timestamp(string, self.now)

    def test_datetime(self):
        self.assertEqual(self.parse("now"), self.ts)
        self.assertEqual(self.parse("-1h"), self.ts - 3600)
        self.assertEqual(self.parse("2021-06-10 08:00:00"), self.ts)
        self.assertEqual(self.parse("20210610"), ts(2021, 6, 10))

    def test_timestamp(self):
        self.assertEqual(self.parse("0"), 0.0)
        self.assertEqual(self.parse("1623340800"), 1623340800.0)
        self.assertEqual(self.parse("1623340800.5"), 1623340800.5)
        self.assertEqual(self.parse("1e9"), 1e9)

    def test_iso8601_basic_date_invalid(self):
        # Exactly eight digits is never treated as a raw timestamp
        with self.assertRaisesRegex(ValueError, "Invalid datetime"):
            self.parse("20211301")  # month 13
        with self.assertRaisesRegex(ValueError, "Invalid datetime"):
            self.parse("20210229")  # not a leap year

    def test_invalid(self):
        # The original parse_datetime() error is raised
        with self.assertRaisesRegex(ValueError, "Invalid datetime"):
            self.parse("blursday")
        with self.assertRaisesRegex(ValueError, "invalid Flux standard duration"):
            self.parse("-1g")
        with self.assertRaisesRegex(ValueError, "Invalid datetime"):
            self.parse("")


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


class TestUtilFormatter(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        # Set timezone to UTC so rendered datetimes match expected value
        os.environ["TZ"] = "UTC"
        time.tzset()
        self.fmt = UtilFormatter()
        self.ts = 978336000.0  # 2001-01-01T08:00:00 UTC

    def format(self, fmt, *args, **kwargs):
        return self.fmt.format(fmt, *args, **kwargs)

    def test_conv_d(self):
        # !d converts a timestamp to UtilDatetime
        self.assertEqual(self.format("{0!d}", self.ts), "2001-01-01T08:00:00")
        self.assertEqual(self.format("{0!d:%b%d %R}", self.ts), "Jan01 08:00")
        self.assertEqual(self.format("{0!d:%b%d %R::>12}", self.ts), " Jan01 08:00")
        self.assertEqual(self.format("{0!d:%b%d %R::>12h}", self.ts), " Jan01 08:00")
        # epoch renders as empty string, or "-" with the h suffix
        self.assertEqual(self.format("{0!d}", 0.0), "")
        self.assertEqual(self.format("{0!d:%FT%T::<6}", 0.0), "      ")
        self.assertEqual(self.format("{0!d:::h}", 0.0), "-")
        self.assertEqual(self.format("{0!d:%FT%T::<6h}", 0.0), "-     ")
        # empty string is treated as epoch
        self.assertEqual(self.format("{0!d}", ""), "")
        self.assertEqual(self.format("{0!d:::h}", ""), "-")
        # keyword args work too
        self.assertEqual(self.format("{t!d:%R}", t=self.ts), "08:00")
        # other invalid values raise TypeError
        with self.assertRaises(TypeError):
            self.format("{0!d}", "foo")
        with self.assertRaises(TypeError):
            self.format("{0!d}", None)

    def test_conv_D(self):
        # !D converts a timestamp to an ISO 8601 string
        self.assertEqual(self.format("{0!D}", self.ts), "2001-01-01T08:00:00")
        self.assertEqual(self.format("{0!D:>21}", self.ts), "  2001-01-01T08:00:00")
        self.assertEqual(self.format("{0!D:>21h}", self.ts), "  2001-01-01T08:00:00")
        # epoch and other false values render as empty string
        self.assertEqual(self.format("{0!D}", 0.0), "")
        self.assertEqual(self.format("{0!D}", 0), "")
        self.assertEqual(self.format("{0!D}", ""), "")
        self.assertEqual(self.format("{0!D}", None), "")
        self.assertEqual(self.format("{0!D:h}", 0.0), "-")
        self.assertEqual(self.format("{0!D:>3h}", ""), "  -")
        with self.assertRaises(TypeError):
            self.format("{0!D}", "foo")

    def test_conv_F(self):
        # !F converts seconds to Flux Standard Duration
        self.assertEqual(self.format("{0!F}", 0), "0s")
        self.assertEqual(self.format("{0!F}", 0.0001), "0s")
        self.assertEqual(self.format("{0!F}", 1.5), "1.500s")
        self.assertEqual(self.format("{0!F}", 30), "30s")
        self.assertEqual(self.format("{0!F}", 90), "1.5m")
        self.assertEqual(self.format("{0!F}", 3600), "1h")
        self.assertEqual(self.format("{0!F}", 2 * 86400), "2d")
        self.assertEqual(self.format("{0!F}", math.inf), "inf")
        self.assertEqual(self.format("{0!F:>6}", 90), "  1.5m")
        # empty string renders as empty string, or "-" with the h suffix
        self.assertEqual(self.format("{0!F}", ""), "")
        self.assertEqual(self.format("{0!F:h}", ""), "-")
        # zero duration is "0s" which is an empty output for h suffix
        self.assertEqual(self.format("{0!F:h}", 0), "-")
        self.assertEqual(self.format("{0!F:>5h}", 0), "    -")
        self.assertEqual(self.format("{0!F:>5h}", 90), " 1.5m")
        with self.assertRaises(TypeError):
            self.format("{0!F}", "foo")
        with self.assertRaises(TypeError):
            self.format("{0!F}", None)

    def test_conv_H(self):
        # !H converts seconds to a H:MM:SS timedelta string
        self.assertEqual(self.format("{0!H}", 0), "0:00:00")
        self.assertEqual(self.format("{0!H}", 90), "0:01:30")
        self.assertEqual(self.format("{0!H}", 3600), "1:00:00")
        self.assertEqual(self.format("{0!H}", 86400), "1 day, 0:00:00")
        # fractional seconds are rounded
        self.assertEqual(self.format("{0!H}", 59.6), "0:01:00")
        self.assertEqual(self.format("{0!H}", 59.4), "0:00:59")
        # nonzero values < 1 are rounded up to 1s, not down to zero
        self.assertEqual(self.format("{0!H}", 0.1), "0:00:01")
        self.assertEqual(self.format("{0!H}", 0.9), "0:00:01")
        # infinity renders as "inf" (issue #7912)
        self.assertEqual(self.format("{0!H}", math.inf), "inf")
        self.assertEqual(self.format("{0!H:>8}", math.inf), "     inf")
        self.assertEqual(self.format("{0!H:h}", math.inf), "inf")
        # empty string renders as empty string, or "-" with the h suffix
        self.assertEqual(self.format("{0!H}", ""), "")
        self.assertEqual(self.format("{0!H:h}", ""), "-")
        # zero timedelta is an empty output for h suffix
        self.assertEqual(self.format("{0!H:h}", 0), "-")
        self.assertEqual(self.format("{0!H:>8h}", 0), "       -")
        with self.assertRaises(TypeError):
            self.format("{0!H}", "foo")
        with self.assertRaises(TypeError):
            self.format("{0!H}", None)

    def test_conv_P(self):
        # !P converts a fraction to a percentage
        self.assertEqual(self.format("{0!P}", 0), "0%")
        self.assertEqual(self.format("{0!P}", 0.5), "50%")
        self.assertEqual(self.format("{0!P}", 1.0), "100%")
        self.assertEqual(self.format("{0!P}", 1), "100%")
        self.assertEqual(self.format("{0!P}", 0.12345), "12.3%")
        self.assertEqual(self.format("{0!P}", 0.999), "99.9%")
        self.assertEqual(self.format("{0!P}", 2.5), "250%")
        # values < 1% get two decimal places
        self.assertEqual(self.format("{0!P}", 0.005), "0.50%")
        self.assertEqual(self.format("{0!P}", 0.00123), "0.12%")
        # empty string renders as empty string, or "-" with the h suffix
        self.assertEqual(self.format("{0!P}", ""), "")
        self.assertEqual(self.format("{0!P:h}", ""), "-")
        with self.assertRaises(TypeError):
            self.format("{0!P}", "foo")
        with self.assertRaises(TypeError):
            self.format("{0!P}", None)

    def test_conv_standard(self):
        # standard conversions still work
        self.assertEqual(self.format("{0!s}", 42), "42")
        self.assertEqual(self.format("{0!r}", "foo"), "'foo'")
        self.assertEqual(self.format("{0!a}", "é"), "'\\xe9'")
        with self.assertRaises(ValueError):
            self.format("{0!X}", 42)

    def test_no_conv(self):
        # no conversion: works like a standard Formatter
        self.assertEqual(self.format("{0}", "foo"), "foo")
        self.assertEqual(self.format("{0:>5}", "foo"), "  foo")
        self.assertEqual(self.format("{0:<5d}", 42), "42   ")
        self.assertEqual(self.format("{0:8.2f}", 1234.567), " 1234.57")
        self.assertEqual(self.format("{s} {i}", s="a", i=1), "a 1")

    def test_h_suffix(self):
        # h suffix replaces "empty" values with a hyphen
        self.assertEqual(self.format("{0:h}", ""), "-")
        self.assertEqual(self.format("{0:h}", "0s"), "-")
        self.assertEqual(self.format("{0:h}", 0.0), "-")
        self.assertEqual(self.format("{0:h}", "0.0"), "-")
        self.assertEqual(self.format("{0:h}", "0:00:00"), "-")
        self.assertEqual(self.format("{0:h}", "1970-01-01T00:00:00"), "-")
        self.assertEqual(self.format("{0:h}", None), "None")
        # non-empty values are unchanged
        self.assertEqual(self.format("{0:h}", "foo"), "foo")
        self.assertEqual(self.format("{0:h}", 42), "42")
        self.assertEqual(self.format("{0:h}", 0), "0")
        self.assertEqual(self.format("{0:h}", 1.5), "1.5")
        # h suffix respects width and alignment
        self.assertEqual(self.format("{0:>5h}", ""), "    -")
        self.assertEqual(self.format("{0:<5h}", ""), "-    ")
        self.assertEqual(self.format("{0:^5h}", ""), "  -  ")
        self.assertEqual(self.format("{0:>5h}", "foo"), "  foo")
        # numeric values are converted to strings, so numeric type
        # specs are not applied, but width still is
        self.assertEqual(self.format("{0:>5h}", 42), "   42")
        self.assertEqual(self.format("{0:<5h}", 42), "42   ")

    def test_truncation_suffix(self):
        # + suffix denotes truncation with a trailing "+"
        self.assertEqual(self.format("{0:5.5+}", "abcdefgh"), "abcd+")
        self.assertEqual(self.format("{0:.3+}", "abcdefgh"), "ab+")
        # no truncation marker when value fits
        self.assertEqual(self.format("{0:5.5+}", "abcde"), "abcde")
        self.assertEqual(self.format("{0:5.5+}", "abc"), "abc  ")
        self.assertEqual(self.format("{0:>5.5+}", "abc"), "  abc")
        self.assertEqual(self.format("{0:5.5+}", ""), "     ")
        # integers are never truncated by width
        self.assertEqual(self.format("{0:>3d+}", 12345), "12345")
        self.assertEqual(self.format("{0:>8d+}", 12345), "   12345")
        # combined with h suffix
        self.assertEqual(self.format("{0:5.5h+}", "abcdefgh"), "abcd+")
        self.assertEqual(self.format("{0:5.5h+}", ""), "-    ")
        # combined with conversions
        self.assertEqual(self.format("{0!D:.10+}", self.ts), "2001-01-0+")
        self.assertEqual(self.format("{0!F:.4+}", 1.5), "1.5+")
        self.assertEqual(self.format("{0!H:.4+}", 90), "0:0+")

    def test_combined(self):
        # multiple fields with different conversions and suffixes
        self.assertEqual(
            self.format(
                "{s:<6h} {i:>7d} {d!d:%b%d %R::^13} {d!D:>19} {f!F:>8h} {p!P:>5}",
                s="",
                i=42,
                d=self.ts,
                f=0,
                p=0.5,
            ),
            "-           42  Jan01 08:00  2001-01-01T08:00:00        -   50%",
        )


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

    def test_sort_empty_keys(self):
        """Test that empty sort keys are skipped (issue #7590)"""
        a = Item("a", 0, 2.2)
        d = Item("d", 33, 1.1)
        z = Item("z", 11, 1.0)

        items = [z, a, d]
        formatter = OutputFormat("{s}:{i}:{f}", headings=self.headings)

        # Trailing comma should be ignored
        formatter.set_sort_keys("s,")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, d, z])

        # Double comma should be ignored
        items = [z, a, d]
        formatter.set_sort_keys("s,,i")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, d, z])

        # Leading comma should be ignored
        items = [z, a, d]
        formatter.set_sort_keys(",s")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, d, z])

        # Multiple trailing commas
        items = [z, a, d]
        formatter.set_sort_keys("s,,,")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, d, z])

        # Whitespace-only key should be ignored
        items = [z, a, d]
        formatter.set_sort_keys("s,  ,i")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, d, z])

        # Multiple empty keys with whitespace variations
        items = [z, a, d]
        formatter.set_sort_keys(" , s , , i , ")
        formatter.sort_items(items)
        self.assertListEqual(items, [a, d, z])

        # Reverse sort with empty keys
        items = [z, a, d]
        formatter.set_sort_keys("-s,,")
        formatter.sort_items(items)
        self.assertListEqual(items, [z, d, a])

    def test_sort_mixed_types(self):
        """Test sorting fields with mixed types (issue #7517)"""
        # Create items with mixed types - some fields are empty strings,
        # others are numbers
        a = Item("a", "", 2.2)  # empty int
        b = Item("b", 10, "")  # empty float
        c = Item("c", 5, 1.5)  # both set
        d = Item("d", "", "")  # both empty

        # Sort by integer field with mixed types (empty strings and ints)
        items = [c, a, b, d]
        formatter = OutputFormat("{s}:{i}:{f}", headings=self.headings)
        formatter.set_sort_keys("i")
        formatter.sort_items(items)
        # Empty strings should sort before numbers
        self.assertListEqual(items, [a, d, c, b])

        # Sort by float field with mixed types (empty strings and floats)
        items = [c, a, b, d]
        formatter.set_sort_keys("f")
        formatter.sort_items(items)
        # Empty strings should sort before numbers
        self.assertListEqual(items, [b, d, c, a])

        # Reverse sort with mixed types
        items = [c, a, b, d]
        formatter.set_sort_keys("-i")
        formatter.sort_items(items)
        # Numbers (reversed) should sort before empty strings
        # Empty strings with equal keys maintain input order (stable sort)
        self.assertListEqual(items, [b, c, a, d])

        # Test with booleans in the mix
        e = Item("e", True, 3.3)
        f = Item("f", False, 4.4)
        g = Item("g", 1, 5.5)
        items = [g, e, a, f, c]
        formatter.set_sort_keys("i")
        formatter.sort_items(items)
        # Order: empty < False(0) < True(1) == 1 < 5
        # True and 1 are numerically equal, so stable sort preserves input
        # order (g before e)
        self.assertListEqual(items, [a, f, g, e, c])

        # Test with string types - numeric strings parse as numbers
        h = Item("h", "100", 6.6)  # string that looks like number
        i = Item("i", "abc", 7.7)  # regular string
        j = Item("j", "10", 8.8)  # another numeric string
        items = [i, c, h, a, j]
        formatter.set_sort_keys("i")
        formatter.sort_items(items)
        # Order: empty < numbers (5 < 10 < 100, parsed from strings) < non-numeric strings
        self.assertListEqual(items, [a, c, j, h, i])

        # Test None, hyphen, and unset values
        k = Item("k", None, 9.9)  # None value
        hyphen = Item("l", "-", 10.0)  # hyphen (also treated as unset)
        items = [c, k, a, hyphen]
        formatter.set_sort_keys("i")
        formatter.sort_items(items)
        # Order: None/empty/hyphen all sort first, then numbers
        # Within unset values, stable sort maintains input order
        self.assertListEqual(items, [k, a, hyphen, c])

        # Test negative numbers (int and string)
        m = Item("m", -5, 11.0)  # negative int
        n = Item("n", "-10", 12.0)  # negative numeric string
        items = [c, m, n, a]
        formatter.set_sort_keys("i")
        formatter.sort_items(items)
        # Order: empty < -10 < -5 < 5
        self.assertListEqual(items, [a, n, m, c])

        # Test float strings
        o = Item("o", "3.14", 13.0)  # float string
        p = Item("p", "2.5", 14.0)  # another float string
        items = [c, o, p]
        formatter.set_sort_keys("i")
        formatter.sort_items(items)
        # Order: 2.5 < 3.14 < 5
        self.assertListEqual(items, [p, o, c])

    def test_display_value(self):
        """Test DisplayValue class corner cases directly"""
        # Partially numeric strings should fall back to string comparison
        # "10abc" cannot be parsed as float, so sorts as string after numbers
        self.assertTrue(DisplayValue(100) < DisplayValue("10abc"))
        self.assertTrue(DisplayValue("10abc") < DisplayValue("abc"))

        # Scientific notation should parse as numeric
        self.assertTrue(DisplayValue("1e5") < DisplayValue(1000000))
        self.assertEqual(
            DisplayValue("1e5")._type_order, DisplayValue.NUMERIC
        )  # numeric type
        self.assertAlmostEqual(DisplayValue("1e5")._sort_value, 100000.0)

        # Equality: numeric string should equal numeric value
        self.assertEqual(DisplayValue("10"), DisplayValue(10))
        self.assertEqual(DisplayValue("10.0"), DisplayValue(10.0))
        self.assertEqual(DisplayValue(True), DisplayValue(1))

        # All unset values should be equal
        self.assertEqual(DisplayValue(None), DisplayValue(""))
        self.assertEqual(DisplayValue(""), DisplayValue("-"))
        self.assertEqual(DisplayValue(None), DisplayValue("-"))

        # Type order verification
        self.assertEqual(DisplayValue(None)._type_order, DisplayValue.EMPTY)  # unset
        self.assertEqual(DisplayValue("")._type_order, DisplayValue.EMPTY)  # unset
        self.assertEqual(DisplayValue("-")._type_order, DisplayValue.EMPTY)  # unset
        self.assertEqual(DisplayValue(5)._type_order, DisplayValue.NUMERIC)  # numeric
        self.assertEqual(
            DisplayValue("10")._type_order, DisplayValue.NUMERIC
        )  # parsed numeric
        self.assertEqual(DisplayValue("abc")._type_order, DisplayValue.STRING)  # string

        # Negative numbers
        self.assertTrue(DisplayValue("-10") < DisplayValue("10"))
        self.assertTrue(DisplayValue("-10") < DisplayValue(0))

        # Float vs int - should compare numerically
        self.assertEqual(DisplayValue(5), DisplayValue(5.0))
        self.assertEqual(DisplayValue("5"), DisplayValue(5.0))

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
