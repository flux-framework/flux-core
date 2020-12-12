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

import json
import unittest
import subflux
from pycotap import TAPTestRunner
from flux.resource import ResourceSet, Rlist
from flux.idset import IDset
from flux.hostlist import Hostlist


class TestRSet(unittest.TestCase):
    R_input = """
    {
      "version": 1,
      "execution": {
        "R_lite": [
          {
            "rank": "0-3",
            "children": {
                "core": "0-3",
                "gpu": "0"
             }
          }
        ],
        "starttime": 0,
        "expiration": 0,
        "nodelist": [
          "fluke[0-3]"
        ]
      }
    }
    """

    def test_init_string(self):
        #  init by string
        rset = ResourceSet(self.R_input)
        self.assertEqual(str(rset), "rank[0-3]/core[0-3],gpu0")
        self.assertEqual(rset.ncores, 16)
        self.assertEqual(rset.ngpus, 4)

    def test_init_dict(self):
        #  init by dict
        rdict = json.loads(self.R_input)
        rset = ResourceSet(rdict)
        self.assertEqual(str(rset), "rank[0-3]/core[0-3],gpu0")
        self.assertEqual(rset.ncores, 16)
        self.assertEqual(rset.ngpus, 4)
        self.assertEqual(rset.nnodes, 4)

    def test_init_implementation(self):
        #  init by resource set implementation
        rlist = Rlist().add_rank(0, cores="0-1").add_child(0, "gpu", "0")
        rset = ResourceSet(rlist)
        self.assertEqual(str(rset), "rank0/core[0-1],gpu0")
        self.assertEqual(rset.ncores, 2)
        self.assertEqual(rset.ngpus, 1)
        self.assertEqual(rset.nnodes, 1)

    def test_init_empty(self):
        rset = ResourceSet()
        self.assertEqual(str(rset), "")
        self.assertEqual(rset.nnodes, 0)
        self.assertEqual(rset.ncores, 0)
        self.assertEqual(rset.ngpus, 0)

    def test_init_assertions(self):
        #  test invalid implementation
        with self.assertRaises(TypeError):
            ResourceSet((1, 2))

        #  test invalid resource set string:
        with self.assertRaises(ValueError):
            ResourceSet("")

        #  test invalid version
        with self.assertRaises(ValueError):
            ResourceSet({"version": 199})

        #  test string with invalid version
        with self.assertRaises(ValueError):
            arg = json.dumps({"version": 199})
            ResourceSet(arg)

        #  test dict without 'version' string
        with self.assertRaises(KeyError):
            ResourceSet({})

        #  test invalid JSON string
        with self.assertRaises(json.decoder.JSONDecodeError):
            ResourceSet("{")

    def test_op(self):
        set1 = ResourceSet(self.R_input)
        set2 = set1.copy().remove_ranks("2-3")

        # difference
        result = set1 - set2
        self.assertIsInstance(result, ResourceSet)
        self.assertEqual(str(result), "rank[2-3]/core[0-3],gpu0")

        set3 = result
        # union
        result = set3 | set1
        self.assertIsInstance(result, ResourceSet)
        self.assertEqual(str(result), "rank[0-3]/core[0-3],gpu0")

        # intersection
        result = set1 & set2
        self.assertIsInstance(result, ResourceSet)
        self.assertEqual(str(result), "rank[0-1]/core[0-3],gpu0")

    def test_encode(self):
        rset = ResourceSet(self.R_input)
        rstring = rset.encode()
        rset2 = ResourceSet(rstring)
        self.assertEqual(str(rset), str(rset2))

    def test_append(self):
        rset = ResourceSet(self.R_input)
        rset2 = ResourceSet(Rlist().add_rank(4, cores="0-3").add_child(4, "gpu", "0"))
        rset.append(rset2)
        self.assertEqual(str(rset), "rank[0-4]/core[0-3],gpu0")

    def test_nodelist(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset.nodelist, Hostlist)
        self.assertEqual(rset.nodelist.count(), 4)
        self.assertEqual(str(rset.nodelist), "fluke[0-3]")

    def test_ranks(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset.ranks, IDset)
        self.assertEqual(rset.ranks.count(), 4)
        self.assertEqual(str(rset.ranks), "0-3")

    def test_state(self):
        rset = ResourceSet(self.R_input)
        self.assertIsNone(rset.state)
        rset.state = "up"
        self.assertEqual(rset.state, "up")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
