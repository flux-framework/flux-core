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

import subflux  # noqa: F401 - for PYTHONPATH
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.resource import ResourceSet, Rlist
from pycotap import TAPTestRunner


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
    R2 = """
    {
      "version": 1,
      "execution": {
        "R_lite": [
          {
            "rank": "10-13",
            "children": {
                "core": "0-3",
                "gpu": "0"
             }
          }
        ],
        "starttime": 0,
        "expiration": 0,
        "nodelist": [
          "fluke[10-13]"
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

        rset = ResourceSet(self.R2)
        self.assertEqual(str(rset), "rank[10-13]/core[0-3],gpu0")
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

        rdict = json.loads(self.R2)
        rset = ResourceSet(rdict)
        self.assertEqual(str(rset), "rank[10-13]/core[0-3],gpu0")
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

        rset = ResourceSet(self.R2)
        rstring = rset.encode()
        rset2 = ResourceSet(rstring)
        self.assertEqual(str(rset), str(rset2))

    def test_append(self):
        rset = ResourceSet(self.R_input)
        rset2 = ResourceSet(Rlist().add_rank(4, cores="0-3").add_child(4, "gpu", "0"))
        rset.append(rset2)
        self.assertEqual(str(rset), "rank[0-4]/core[0-3],gpu0")

    def test_add(self):
        rset = ResourceSet(self.R_input)
        rset2 = ResourceSet(Rlist().add_rank(4, cores="0-3").add_child(4, "gpu", "0"))
        rset.add(rset2)
        self.assertEqual(str(rset), "rank[0-4]/core[0-3],gpu0")
        # adding same resources allowed
        rset2 = ResourceSet(Rlist().add_rank(4, cores="0-3").add_child(4, "gpu", "0"))
        rset.add(rset2)
        self.assertEqual(str(rset), "rank[0-4]/core[0-3],gpu0")

    def test_nodelist(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset.nodelist, Hostlist)
        self.assertEqual(rset.nodelist.count(), 4)
        self.assertEqual(str(rset.nodelist), "fluke[0-3]")

        rset = ResourceSet(self.R2)
        self.assertIsInstance(rset.nodelist, Hostlist)
        self.assertEqual(rset.nodelist.count(), 4)
        self.assertEqual(str(rset.nodelist), "fluke[10-13]")

    def test_ranks(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset.ranks, IDset)
        self.assertEqual(rset.ranks.count(), 4)
        self.assertEqual(str(rset.ranks), "0-3")

        rset = ResourceSet(self.R2)
        self.assertIsInstance(rset.ranks, IDset)
        self.assertEqual(rset.ranks.count(), 4)
        self.assertEqual(str(rset.ranks), "10-13")

    def test_state(self):
        rset = ResourceSet(self.R_input)
        self.assertIsNone(rset.state)
        rset.state = "up"
        self.assertEqual(rset.state, "up")

    def test_host_ranks(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset, ResourceSet)
        self.assertEqual(rset.host_ranks("fluke0"), [0])
        self.assertEqual(rset.host_ranks("fluke1"), [1])
        self.assertEqual(rset.host_ranks("fluke2"), [2])
        self.assertEqual(rset.host_ranks("fluke3"), [3])
        self.assertEqual(rset.host_ranks("fluke[0,3]"), [0, 3])
        self.assertEqual(rset.host_ranks("fluke[0-2]"), [0, 1, 2])
        self.assertEqual(
            rset.host_ranks("fluke[0-2,7]", ignore_nomatch=True), [0, 1, 2]
        )
        with self.assertRaises(FileNotFoundError):
            rset.host_ranks("fluke7")

        rset = ResourceSet(self.R2)
        self.assertIsInstance(rset, ResourceSet)
        self.assertEqual(rset.host_ranks("fluke10"), [10])
        self.assertEqual(rset.host_ranks("fluke11"), [11])
        self.assertEqual(rset.host_ranks("fluke12"), [12])
        self.assertEqual(rset.host_ranks("fluke13"), [13])
        self.assertEqual(rset.host_ranks("fluke[10,13]"), [10, 13])
        self.assertEqual(rset.host_ranks("fluke[10-12]"), [10, 11, 12])
        self.assertEqual(
            rset.host_ranks("fluke[0-2,10-12]", ignore_nomatch=True), [10, 11, 12]
        )
        with self.assertRaises(FileNotFoundError):
            rset.host_ranks("fluke[0-3,10-12]")

    def test_properties(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset, ResourceSet)

        rset.set_property("xx", "0-1")
        rset.set_property("zz", "2-3")
        rset.set_property("aa", "1")

        with self.assertRaises(ValueError):
            rset.set_property("x^y")
        with self.assertRaises(ValueError):
            rset.set_property("yy", "0-6")
        with self.assertRaises(ValueError):
            rset.set_property("yy", "foo")

        # copy_constraint() with invalid property returns empty set
        empty = rset.copy_constraint({"properties": ["foo"]})
        self.assertIsInstance(empty, ResourceSet)
        self.assertEqual(str(empty), "")

        xx = rset.copy_constraint({"properties": ["xx"]})
        self.assertIsInstance(xx, ResourceSet)
        self.assertEqual(str(xx), "rank[0-1]/core[0-3],gpu0")

        zz = rset.copy_constraint({"properties": ["zz"]})
        self.assertIsInstance(zz, ResourceSet)
        self.assertEqual(str(zz), "rank[2-3]/core[0-3],gpu0")

        aa = rset.copy_constraint({"properties": ["aa"]})
        self.assertIsInstance(aa, ResourceSet)
        self.assertEqual(str(aa), "rank1/core[0-3],gpu0")

        aa = xx.copy_constraint({"properties": ["aa"]})
        self.assertIsInstance(aa, ResourceSet)
        self.assertEqual(str(aa), "rank1/core[0-3],gpu0")

        #  not (xx and aa)
        notax = rset.copy_constraint({"not": [{"properties": ["aa", "xx"]}]})
        self.assertIsInstance(notax, ResourceSet)
        self.assertEqual(str(notax), "rank[0,2-3]/core[0-3],gpu0")

        #  not xx and not aa
        notax2 = rset.copy_constraint({"properties": ["^aa", "^xx"]})
        self.assertIsInstance(notax2, ResourceSet)
        self.assertEqual(str(notax2), "rank[2-3]/core[0-3],gpu0")

        with self.assertRaises(ValueError):
            rset.copy_constraint({"foo": []})
        with self.assertRaises(ValueError):
            rset.copy_constraint({"properties": [42]})

    def test_count(self):
        rset = ResourceSet(self.R_input)
        self.assertEqual(rset.count("core"), 16)
        self.assertEqual(rset.count("gpu"), 4)
        # unknown resource type returns 0
        self.assertEqual(rset.count("notaresource"), 0)

    def test_rlist(self):
        rset = ResourceSet(self.R_input)
        self.assertEqual(rset.rlist, str(rset))
        self.assertEqual(rset.rlist, "rank[0-3]/core[0-3],gpu0")

    def test_copy(self):
        rset = ResourceSet(self.R_input)
        rset.state = "up"
        cp = rset.copy()
        self.assertIsInstance(cp, ResourceSet)
        self.assertEqual(str(cp), str(rset))
        self.assertEqual(cp.state, "up")
        # Verify it's a real copy (modifications don't affect original)
        cp.remove_ranks("0")
        self.assertEqual(str(rset), "rank[0-3]/core[0-3],gpu0")

    def test_copy_ranks(self):
        rset = ResourceSet(self.R_input)
        rset.state = "up"

        # copy single rank
        cp = rset.copy_ranks("0")
        self.assertIsInstance(cp, ResourceSet)
        self.assertEqual(str(cp), "rank0/core[0-3],gpu0")
        self.assertEqual(cp.state, "up")

        # copy multiple ranks as string
        cp = rset.copy_ranks("1-2")
        self.assertEqual(str(cp), "rank[1-2]/core[0-3],gpu0")

        # copy via IDset
        cp = rset.copy_ranks(IDset("0,3"))
        self.assertEqual(str(cp), "rank[0,3]/core[0-3],gpu0")

    def test_remove_ranks(self):
        rset = ResourceSet(self.R_input)
        rset.remove_ranks("0-1")
        self.assertEqual(str(rset), "rank[2-3]/core[0-3],gpu0")
        self.assertEqual(rset.nnodes, 2)

        # remove via IDset
        rset = ResourceSet(self.R_input)
        rset.remove_ranks(IDset("0,3"))
        self.assertEqual(str(rset), "rank[1-2]/core[0-3],gpu0")

    def test_get_properties(self):
        rset = ResourceSet(self.R_input)
        rset.set_property("foo", "0-1")
        rset.set_property("bar", "2-3")
        props = json.loads(rset.get_properties())
        self.assertIsInstance(props, dict)
        self.assertIn("foo", props)
        self.assertIn("bar", props)

    def test_properties_attribute(self):
        rset = ResourceSet(self.R_input)
        # no properties set
        self.assertEqual(rset.properties, "")

        rset.set_property("foo", "0-1")
        rset.set_property("bar", "2-3")
        prop_list = rset.properties.split(",")
        self.assertIn("foo", prop_list)
        self.assertIn("bar", prop_list)

    def test_set_property_all_ranks(self):
        rset = ResourceSet(self.R_input)
        # set property on all ranks (no ranks arg)
        rset.set_property("all")
        result = rset.copy_constraint({"properties": ["all"]})
        self.assertEqual(str(result), str(rset))

    def test_expiration(self):
        rset = ResourceSet(self.R_input)
        # default expiration from R_input is 0
        self.assertEqual(rset.expiration, 0.0)

        rset.expiration = 12345.0
        self.assertEqual(rset.expiration, 12345.0)

        # round-trip through encode/decode preserves expiration
        rset2 = ResourceSet(rset.encode())
        self.assertEqual(rset2.expiration, 12345.0)

    def test_starttime(self):
        rset = ResourceSet(self.R_input)
        # default starttime from R_input is 0
        self.assertEqual(rset.starttime, 0.0)

        rset.starttime = 9999.0
        self.assertEqual(rset.starttime, 9999.0)

        # round-trip through encode/decode preserves starttime
        rset2 = ResourceSet(rset.encode())
        self.assertEqual(rset2.starttime, 9999.0)

    def test_starttime_expiration_together(self):
        rset = ResourceSet(self.R_input)
        rset.starttime = 1000.0
        rset.expiration = 2000.0
        self.assertEqual(rset.starttime, 1000.0)
        self.assertEqual(rset.expiration, 2000.0)

        rset2 = ResourceSet(rset.encode())
        self.assertEqual(rset2.starttime, 1000.0)
        self.assertEqual(rset2.expiration, 2000.0)

    def test_multi_arg_ops(self):
        rset = ResourceSet(self.R_input)  # ranks 0-3
        r2 = ResourceSet(self.R2)  # ranks 10-13
        extra = ResourceSet(Rlist().add_rank(5, cores="0-1"))

        # multi-arg union
        result = rset.union(r2, extra)
        self.assertIn("5", str(result.ranks))
        self.assertIn("10", str(result.ranks))
        self.assertIn("0", str(result.ranks))

        # multi-arg diff
        rset_copy = ResourceSet(self.R_input)
        r_01 = rset_copy.copy_ranks("0-1")
        r_23 = rset_copy.copy_ranks("2-3")
        result = rset_copy.diff(r_01, r_23)
        self.assertEqual(str(result), "")

        # multi-arg intersect
        rset_copy = ResourceSet(self.R_input)
        r_0_2 = rset_copy.copy_ranks("0-2")
        r_1_3 = rset_copy.copy_ranks("1-3")
        result = rset_copy.intersect(r_0_2, r_1_3)
        self.assertEqual(str(result.ranks), "1-2")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
