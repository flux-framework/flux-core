#!/usr/bin/env python3
###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest
import subflux
from pycotap import TAPTestRunner
import flux.hostlist as hostlist


class TestHostlistMethods(unittest.TestCase):
    def test_basic_decode(self):
        # simple string works
        hl = hostlist.decode("host")
        self.assertEqual(hl.count(), 1)

        # iterables works
        hl = hostlist.decode(["foo1", "foo2"])
        self.assertEqual(str(hl), "foo[1-2]")

        # set works, but must sort to get guaranteed order
        hl = hostlist.decode({"foo1", "foo2"}).sort()
        self.assertEqual(str(hl), "foo[1-2]")

        with self.assertRaises(TypeError):
            hl = hostlist.decode(["foo1", 42])

    def test_invalid_decode(self):
        test_invalid = [
            "[]",
            "foo[]",
            "foo[",
            "foo[1,3",
            "foo[[1,3]",
            "foo]",
            "foo[x-y]",
            "foo[0-1,2--5]",
        ]
        for string in test_invalid:
            with self.assertRaises(ValueError):
                hostlist.decode(string)

        # TypeError tests
        with self.assertRaises(TypeError):
            hostlist.decode(42)
        with self.assertRaises(TypeError):
            hostlist.decode(1.0)
        with self.assertRaises(TypeError):
            hostlist.decode(["foo", 42])
        with self.assertRaises(TypeError):
            hostlist.decode()

    def test_str(self):
        tests = [
            {"input": "", "output": ""},
            {"input": "foo0", "output": "foo0"},
            {"input": "foo0,foo1", "output": "foo[0-1]"},
            {"input": "foo0,bar1", "output": "foo0,bar1"},
            {"input": "foo[0-10]", "output": "foo[0-10]"},
        ]
        for test in tests:
            hl = hostlist.decode(test["input"])
            expected = test["output"]
            self.assertEqual(str(hl), expected)
            self.assertEqual(repr(hl), f"Hostlist('{expected}')")

    def test_count(self):
        tests = [
            {"input": "", "result": 0},
            {"input": "foo0", "result": 1},
            {"input": "foo0,foo1", "result": 2},
            {"input": "foo0,bar1", "result": 2},
            {"input": "foo[0-10]", "result": 11},
        ]
        for test in tests:
            hl = hostlist.decode(test["input"])
            self.assertEqual(len(hl), test["result"])
            self.assertEqual(hl.count(), test["result"])

    def test_index(self):
        hl = hostlist.decode("foo[0-9]")
        self.assertEqual(hl[0], "foo0")
        self.assertEqual(hl[1], "foo1")
        self.assertEqual(hl[9], "foo9")
        self.assertEqual(hl[-1], "foo9")
        self.assertEqual(hl[-2], "foo8")
        self.assertListEqual(list(hl[1:3]), ["foo1", "foo2"])

    def test_index_exceptions(self):
        hl = hostlist.decode("foo[0-9]")
        self.assertRaises(TypeError, lambda x: x["a"], hl)
        self.assertRaises(IndexError, lambda x: x[10], hl)

    def test_contains(self):
        hl = hostlist.decode("foo[0-9]")
        self.assertIn("foo0", hl)
        self.assertIn("foo5", hl)
        self.assertNotIn("foo10", hl)
        self.assertNotIn("foo", hl)

    def test_iterator(self):
        hl = hostlist.decode("foo[0-3]")
        self.assertListEqual([host for host in hl], hl.expand())
        hl = hostlist.decode("")
        self.assertListEqual([host for host in hl], [])

    def test_append(self):
        hl = hostlist.decode("")
        self.assertEqual(hl.append("foo[0-3]"), 4)
        self.assertEqual(str(hl), "foo[0-3]")
        self.assertEqual(hl.append("foo[7-9]"), 3)
        self.assertEqual(str(hl), "foo[0-3,7-9]")
        nl = hostlist.decode("foo1,bar")
        self.assertEqual(hl.append(nl), 2)
        self.assertEqual(str(hl), "foo[0-3,7-9,1],bar")
        hl.append(["bar0", "bar1"])
        self.assertEqual(str(hl), "foo[0-3,7-9,1],bar,bar[0-1]")

        with self.assertRaises(TypeError):
            hl.append(42)
        with self.assertRaises(TypeError):
            hl.append(["bar2", 42])

    def test_delete(self):
        hl = hostlist.decode("foo[0-10]")
        self.assertEqual(hl.delete("foo5"), 1)
        self.assertEqual(hl.delete("foo1,foo3"), 2)
        self.assertEqual(str(hl), "foo[0,2,4,6-10]")

        self.assertEqual(hl.delete(hostlist.decode("foo[6-7]")), 2)
        self.assertEqual(str(hl), "foo[0,2,4,8-10]")

    def test_sort(self):
        hl = hostlist.decode("foo[7,6,5,4,3,2,1]").sort()
        self.assertEqual(str(hl), "foo[1-7]")

    def test_uniq(self):
        hl = hostlist.decode("foo[1-7,1-7,1-7]").uniq()
        self.assertEqual(str(hl), "foo[1-7]")

    def test_copy(self):
        hl = hostlist.decode("foo[0-3]")
        cp = hl.copy()
        hl.delete("foo[0-3]")
        self.assertEqual(str(cp), "foo[0-3]")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
