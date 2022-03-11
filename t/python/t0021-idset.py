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
import flux.idset as idset


class TestIDsetMethods(unittest.TestCase):
    def test_constructor(self):
        self.assertEqual(str(idset.IDset()), "")
        self.assertEqual(str(idset.IDset(idset.IDset())), "")
        self.assertEqual(str(idset.IDset(0)), "0")
        self.assertEqual(str(idset.IDset(42)), "42")
        self.assertEqual(str(idset.IDset([42])), "42")
        self.assertEqual(str(idset.IDset("40,41,42")), "40-42")
        self.assertEqual(str(idset.IDset([40, 41, 42])), "40-42")
        self.assertEqual(str(idset.IDset([42, 41, 40])), "40-42")

    def test_str(self):
        tests = [
            {"input": "", "flags": None, "output": ""},
            {"input": "0", "flags": None, "output": "0"},
            {"input": "0,1", "flags": None, "output": "0-1"},
            {"input": "0-10", "flags": None, "output": "0-10"},
            {
                "input": "0-10",
                "flags": idset.IDSET_FLAG_RANGE | idset.IDSET_FLAG_BRACKETS,
                "output": "[0-10]",
            },
            {
                "input": "0-3",
                "flags": idset.IDSET_FLAG_BRACKETS,
                "output": "[0,1,2,3]",
            },
            {"input": "0-3", "flags": 0, "output": "0,1,2,3"},
        ]
        for test in tests:
            ids = idset.decode(test["input"])
            if test["flags"] is not None:
                ids.set_flags(test["flags"])
            self.assertEqual(str(ids), test["output"])
            expected = str(idset.decode(test["output"]))
            self.assertEqual(repr(ids), f"IDset('{expected}')")

    def test_count(self):
        tests = [
            {"input": "", "result": 0},
            {"input": "0", "result": 1},
            {"input": "0,1", "result": 2},
            {"input": "0-10", "result": 11},
            {"input": "0,5,9", "result": 3},
        ]
        i = 0
        for test in tests:
            with self.subTest(i=i):
                ids = idset.decode(test["input"])
                self.assertEqual(len(ids), test["result"])
                self.assertEqual(ids.count(), test["result"])

    def test_index(self):
        ids = idset.decode("0-9")
        self.assertTrue(ids[0])
        self.assertTrue(ids[5])
        self.assertFalse(ids[10])
        ids[10] = True
        ids[21] = 1
        ids[5] = False
        self.assertTrue(ids[10])
        self.assertTrue(ids[21])
        self.assertFalse(ids[5])
        self.assertEqual(str(ids), "0-4,6-10,21")

    def test_index_exceptions(self):
        ids = idset.decode("0-9")
        self.assertRaises(TypeError, lambda x: x["a"], ids)
        self.assertRaises(ValueError, lambda x: x[-1], ids)
        with self.assertRaises(TypeError):
            ids[0] = 7
        with self.assertRaises(TypeError):
            ids[0] = "hello"

    def test_contains(self):
        ids = idset.decode("0-9")
        self.assertIn(0, ids)
        self.assertIn(5, ids)
        self.assertNotIn(10, ids)
        self.assertNotIn(1024, ids)

    def test_contains_exceptions(self):
        ids = idset.decode("0-9")
        with self.assertRaises(ValueError):
            -1 in ids
        with self.assertRaises(TypeError):
            "foo" in ids

    def test_iterator(self):
        ids = idset.decode("0-3,7")
        self.assertListEqual([i for i in ids], [0, 1, 2, 3, 7])
        ids = idset.decode("3")
        self.assertListEqual([i for i in ids], [3])
        ids = idset.decode("")
        self.assertListEqual([i for i in ids], [])

    def test_expand(self):
        self.assertListEqual(idset.decode("0-3").expand(), [0, 1, 2, 3])
        self.assertListEqual(idset.decode("").expand(), [])

    def test_set_and_clear(self):
        ids = idset.IDset()
        ids.set(3)
        self.assertTrue(ids[3])
        self.assertEqual(str(ids), "3")
        ids.clear(3)
        self.assertFalse(ids[3])
        self.assertEqual(str(ids), "")

        ids.set(3, 7)
        self.assertEqual(str(ids), "3-7")

        ids.clear(4, 5)
        self.assertEqual(str(ids), "3,6-7")

    def test_set_and_clear_exceptions(self):
        ids = idset.IDset()
        self.assertRaises(ValueError, lambda x: x.set(-1), ids)
        self.assertRaises(ValueError, lambda x: x.set(1, -1), ids)
        self.assertRaises(TypeError, lambda x: x.set("a"), ids)
        self.assertRaises(ValueError, lambda x: x.set(5, 1), ids)

        self.assertRaises(ValueError, lambda x: x.clear(-1), ids)
        self.assertRaises(ValueError, lambda x: x.clear(1, -1), ids)
        self.assertRaises(TypeError, lambda x: x.clear("a"), ids)
        self.assertRaises(ValueError, lambda x: x.clear(5, 1), ids)

    def test_copy(self):
        ids = idset.decode("0-9")
        cp = ids.copy()
        cp.clear(1, 9)
        self.assertEqual(str(cp), "0")

    def test_equal(self):
        ids1 = idset.IDset()
        ids2 = idset.IDset()
        self.assertEqual(ids1, ids2)
        self.assertTrue(ids1.equal(ids2))
        ids1.set(0, 9)
        ids2.set(0, 9)
        self.assertEqual(ids1, ids2)
        self.assertTrue(ids2.equal(ids1))
        ids1.clear(0)
        self.assertNotEqual(ids1, ids2)
        self.assertFalse(ids1.equal(ids2))
        with self.assertRaises(TypeError):
            ids1 == "foo"
        with self.assertRaises(TypeError):
            ids1.equal("foo")

    def test_first_last_next(self):
        ids = idset.decode("0-9")
        self.assertEqual(ids.first(), 0)
        self.assertEqual(ids.last(), 9)
        self.assertEqual(ids.next(5), 6)
        self.assertEqual(ids.next(9), idset.IDSET_INVALID_ID)
        self.assertRaises(ValueError, lambda x: x.next(-1), ids)
        self.assertRaises(TypeError, lambda x: x.next("a"), ids)

    def test_add_subtract(self):
        ids = idset.decode("0-9")
        ids2 = ids.copy()

        self.assertEqual(str(ids.add("10-11")), "0-11")
        self.assertEqual(str(ids.add([20, 21])), "0-11,20-21")
        self.assertEqual(str(ids.add(idset.decode(""))), "0-11,20-21")

        ids2 += "10-11"
        self.assertEqual(str(ids2), "0-11")
        ids2 += [20, 21]
        self.assertEqual(str(ids2), "0-11,20-21")
        ids2 += idset.decode("")
        self.assertEqual(str(ids2), "0-11,20-21")

        self.assertEqual(str(ids.subtract([])), "0-11,20-21")
        self.assertEqual(str(ids.subtract("11-20")), "0-10,21")
        self.assertEqual(str(ids.subtract(idset.decode("0-10"))), "21")
        self.assertEqual(str(ids.subtract([21])), "")

        ids2 -= ""
        self.assertEqual(str(ids2), "0-11,20-21")
        ids2 -= idset.IDset()
        self.assertEqual(str(ids2), "0-11,20-21")
        ids2 -= "11-20"
        self.assertEqual(str(ids2), "0-10,21")
        ids2 -= idset.decode("0-10")
        self.assertEqual(str(ids2), "21")
        ids2 -= 21
        self.assertEqual(str(ids2), "")

        with self.assertRaises(ValueError):
            ids.subtract("foo")
        with self.assertRaises(TypeError):
            ids.subtract(42.0)
        with self.assertRaises(ValueError):
            ids.add("foo")
        with self.assertRaises(TypeError):
            ids.add(42.0)

    def test_intersect(self):
        tests = [
            {"idset": "0-10", "args": ["0-5", "0-3"], "result": "0-3"},
            {"idset": "0-1", "args": ["3-4"], "result": ""},
            {"idset": "0-1024", "args": ["500-600"], "result": "500-600"},
        ]
        for test in tests:
            ids = idset.decode(test["idset"])
            # first, works with encoded idsets
            result = ids.intersect(*test["args"])
            self.assertEqual(str(result), test["result"])

            # also try with decoded IDset objects
            result = ids.intersect(*map(idset.decode, test["args"]))
            self.assertEqual(str(result), test["result"])

            # and finally with & operator
            result = ids.copy()
            for arg in test["args"]:
                result = result & arg
            self.assertEqual(str(result), test["result"])

    def test_union(self):
        tests = [
            {"idset": "0-10", "args": ["5-15", "0-3"], "result": "0-15"},
            {"idset": "0-1", "args": ["3-4"], "result": "0-1,3-4"},
        ]
        for test in tests:
            ids = idset.decode(test["idset"])
            result = ids.union(*test["args"])
            self.assertEqual(str(result), test["result"])

            result = ids.copy()
            for arg in test["args"]:
                result = result + arg
            self.assertEqual(str(result), test["result"])

            result = ids.copy()
            for arg in test["args"]:
                result = result | arg
            self.assertEqual(str(result), test["result"])

    def test_difference(self):
        tests = [
            {"idset": "0-10", "args": ["0-3"], "result": "4-10"},
            {"idset": "0-10", "args": ["0-10"], "result": ""},
            {"idset": "0-10", "args": ["5-7", "1-3"], "result": "0,4,8-10"},
            {"idset": "0-1", "args": ["0-10"], "result": ""},
            {"idset": "0-1024", "args": ["500-600"], "result": "0-499,601-1024"},
        ]
        for test in tests:
            ids = idset.decode(test["idset"])
            result = ids.difference(*test["args"])
            self.assertEqual(str(result), test["result"])

            result = ids.copy()
            for arg in test["args"]:
                result = result - arg
            self.assertEqual(str(result), test["result"])


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
