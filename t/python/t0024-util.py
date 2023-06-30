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

import unittest
from datetime import datetime

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.util import UtilDatetime, parse_datetime
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


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
