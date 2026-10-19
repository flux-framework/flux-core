#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest
from itertools import islice

import flux.resourcecount as rc
import subflux  # noqa: F401
from pycotap import TAPTestRunner


class TestResourceCountMethods(unittest.TestCase):
    def test_constructor_string(self):
        self.assertEqual(str(rc.ResourceCount("4")), "4")
        self.assertEqual(str(rc.ResourceCount("2-8")), "2-8")
        #  short form omits the redundant default operator
        self.assertEqual(str(rc.ResourceCount("2-8:2:+")), "2-8:2")
        self.assertEqual(str(rc.ResourceCount("1,7-9")), "1,7-9")
        self.assertEqual(str(rc.ResourceCount("2+")), "2+")
        self.assertEqual(str(rc.ResourceCount("[1-3]")), "1-3")
        self.assertEqual(str(rc.ResourceCount(rc.ResourceCount("4"))), "4")
        self.assertEqual(str(rc.ResourceCount('{"min": 2, "max": 8}')), "2-8")

    def test_constructor_int(self):
        self.assertEqual(str(rc.ResourceCount(4)), "4")

    def test_constructor_dict(self):
        self.assertEqual(str(rc.ResourceCount({"min": 2, "max": 8})), "2-8")
        self.assertEqual(
            str(rc.ResourceCount({"min": 2, "max": 8, "operand": 2, "operator": "*"})),
            "2-8:2:*",
        )
        self.assertEqual(str(rc.ResourceCount({"min": 3})), "3+")

    def test_constructor_invalid(self):
        for bad in ("", "bogus", "0", "-1", "1-", "2-1", "{not json"):
            with self.assertRaises(ValueError):
                rc.ResourceCount(bad)
        with self.assertRaises(ValueError):
            rc.ResourceCount({"min": 8, "max": 2})
        for bad in (None, 1.5, [1, 2]):
            with self.assertRaises(TypeError):
                rc.ResourceCount(bad)

    def test_encode_flags(self):
        c = rc.ResourceCount("2-8:2:*")
        self.assertEqual(c.encode(), "2-8:2:*")
        self.assertEqual(c.encode(flags=0), "2-8:2:*")
        self.assertEqual(c.encode(flags=rc.RESOURCECOUNT_FLAG_BRACKETS), "[2-8:2:*]")
        #  short form omits the default operand and operator
        r = rc.ResourceCount({"min": 2, "max": 8})
        self.assertEqual(r.encode(), "2-8")
        self.assertEqual(r.encode(flags=0), "2-8:1:+")
        self.assertEqual(
            r.encode(
                flags=rc.RESOURCECOUNT_FLAG_BRACKETS | rc.RESOURCECOUNT_FLAG_SHORT
            ),
            "[2-8]",
        )

    def test_encode_invalid_flags(self):
        with self.assertRaises(ValueError):
            rc.ResourceCount("4").encode(flags=0xFF)

    def test_decode(self):
        self.assertEqual(str(rc.decode("4")), "4")

    def test_iterator(self):
        self.assertEqual(list(rc.ResourceCount(4)), [4])
        self.assertEqual(list(rc.ResourceCount("2-4")), [2, 3, 4])
        self.assertEqual(list(rc.ResourceCount("1,7-9")), [1, 7, 8, 9])
        self.assertEqual(list(rc.ResourceCount("2-8:2:+")), [2, 4, 6, 8])
        self.assertEqual(list(rc.ResourceCount("2-8:2:*")), [2, 4, 8])

    def test_iterator_unbounded(self):
        self.assertEqual(list(islice(rc.ResourceCount("2+"), 3)), [2, 3, 4])

    def test_first_next(self):
        c = rc.ResourceCount("2-4")
        self.assertEqual(c.first, 2)
        self.assertEqual(c.next(2), 3)
        self.assertEqual(c.next(3), 4)
        self.assertEqual(c.next(4), rc.RESOURCECOUNT_INVALID_VALUE)
        #  a simple integer has no next value
        self.assertEqual(rc.ResourceCount(4).next(4), rc.RESOURCECOUNT_INVALID_VALUE)

    def test_next_invalid(self):
        c = rc.ResourceCount("2-4")
        with self.assertRaises(TypeError):
            c.next("2")
        with self.assertRaises(ValueError):
            c.next(-1)

    def test_set_flags(self):
        c = rc.ResourceCount("1-3")
        c.set_flags(0)
        self.assertEqual(c.encode(), "1,2,3")
        c.set_flags(rc.RESOURCECOUNT_FLAG_SHORT)
        self.assertEqual(c.encode(), "1-3")

    def test_first_last(self):
        cases = [
            ("4", 4, 4),
            ("2-8", 2, 8),
            ("2+", 2, None),
            ("1,7-9", 1, 9),
            ("2-8:2:*", 2, 8),
            ({"min": 1}, 1, None),
            ({"min": 3, "max": 3}, 3, 3),
        ]
        for spec, first, last in cases:
            with self.subTest(spec=spec):
                count = rc.ResourceCount(spec)
                self.assertEqual(count.first, first)
                self.assertEqual(count.last, last)

    def test_is_discrete(self):
        for spec in ("4", "2-8", "1-3", "2-5:1:+", {"min": 2, "max": 8}):
            with self.subTest(spec=spec):
                self.assertFalse(rc.ResourceCount(spec).is_discrete)
        for spec in ("1,7-9", "2-8:2:*", "2-8:2:+"):
            with self.subTest(spec=spec):
                self.assertTrue(rc.ResourceCount(spec).is_discrete)

    def test_scale(self):
        #  identity scale returns self
        count = rc.ResourceCount("2-8")
        self.assertIs(count.scale(1), count)
        cases = [
            ("2-8", 2, [4, 6, 8, 10, 12, 14, 16]),
            (4, 3, [12]),
            ("1-3", 2, [2, 4, 6]),
            ("1,7-9", 2, [2, 14, 16, 18]),
            ("2-8:2:*", 3, [6, 12, 24]),
        ]
        for spec, factor, values in cases:
            with self.subTest(spec=spec):
                count = rc.ResourceCount(spec).scale(factor)
                self.assertEqual(list(count), values)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
